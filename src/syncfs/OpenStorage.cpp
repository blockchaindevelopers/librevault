/* Copyright (C) 2014-2015 Alexander Shishenko <GamePad64@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "Meta.pb.h"
#include "../../contrib/crypto/AES_CBC.h"
#include <cryptopp/osrng.h>
#include <boost/filesystem/fstream.hpp>
#include <boost/log/trivial.hpp>
#include <fstream>
#include <set>

#include "OpenStorage.h"
#include "SyncFS.h"

namespace librevault {
namespace syncfs {

OpenStorage::OpenStorage(const Key& key, std::shared_ptr<SQLiteDB> directory_db, EncStorage& enc_storage, const fs::path& open_path, const fs::path& block_path) :
		key(key), directory_db(directory_db), enc_storage(enc_storage), open_path(open_path), block_path(block_path) {}
OpenStorage::~OpenStorage() {}

std::pair<blob, blob> OpenStorage::get_both_blocks(const blob& block_hash){
	auto sql_result = directory_db->exec("SELECT blocks.blocksize, blocks.iv, files.path, openfs.offset FROM blocks "
			"LEFT JOIN openfs ON blocks.id = openfs.blockid "
			"LEFT JOIN files ON openfs.fileid = files.id "
			"WHERE blocks.encrypted_hash=:encrypted_hash", {
					{":encrypted_hash", block_hash}
			});

	for(auto row : sql_result){
		uint64_t blocksize	= row[0];
		blob iv				= row[1];
		auto filepath		= fs::absolute(row[2].as_text(), open_path);
		uint64_t offset		= row[3];

		fs::ifstream ifs; ifs.exceptions(std::ios::failbit | std::ios::badbit);
		blob block(blocksize);

		try {
			ifs.open(filepath, std::ios::binary);
			ifs.seekg(offset);
			ifs.read(reinterpret_cast<char*>(block.data()), blocksize);

			blob encblock = crypto::AES_CBC(key.get_Encryption_Key(), iv).encrypt(block);
			// Check
			if(enc_storage.verify_encblock(block_hash, block)) return {block, encblock};
		}catch(const std::ios::failure& e){}
	}
	throw SyncFS::no_such_block();
}

blob OpenStorage::get_encblock(const blob& block_hash) {
	return get_both_blocks(block_hash).second;
}

blob OpenStorage::get_block(const blob& block_hash) {
	return get_both_blocks(block_hash).first;
}

void OpenStorage::assemble(bool delete_blocks){
	auto raii_lock = SQLiteLock(directory_db);
	/*directory_db->exec("SAVEPOINT assemble");
	try {
		auto blocks = directory_db->exec("SELECT files.path_hmac, openfs.[offset], blocks.encrypted_hash, blocks.iv "
				"FROM openfs "
				"JOIN files ON openfs.file_path_hmac=files.path_hmac "
				"JOIN blocks ON openfs.block_encrypted_hash=blocks.encrypted_hash "
				"WHERE assembled=0");
		std::map<blob, std::map<uint64_t, std::pair<blob, blob>>> write_blocks;
		for(auto row : blocks){
			write_blocks[row[0].as_blob()][(uint64_t)row[1].as_int()] = {row[2].as_blob(), row[3].as_blob()};
		}

		fs::ofstream ofs(block_path / "assembled.part", std::ios::out | std::ios::trunc | std::ios::binary);
		for(auto file : write_blocks){
			blob& path_hmac = file.first;
			for(auto block : file.second){
				auto offset = block.first;
				auto block_hash = block.second.first;
				auto iv = block.second.second;
			}
			blob block = parent->get_block(write_block.first);
			ofs.write((const char*)block.data(), block.size());
		}

		ofs.close();

		auto abspath = fs::absolute(relpath, parent->open_path);
		fs::remove(abspath);
		fs::rename(parent->system_path / "assembled.part", abspath);

		parent->directory_db->exec("UPDATE openfs SET assembled=1 WHERE fileid IN (SELECT id FROM files WHERE path=:path)", {
				{":path", relpath.generic_string()}
		});

		parent->directory_db->exec("RELEASE assemble");
	}catch(...){
		parent->directory_db->exec("ROLLBACK TO assemble"); throw;
	}*/
}

void OpenStorage::disassemble(const std::string& file_path, bool delete_file = false){
	blob path_hmac = crypto::HMAC_SHA3_224(key.get_Encryption_Key()).to(file_path);

	auto blocks_data = directory_db->exec("SELECT blocks.encrypted_hash "
			"FROM files JOIN blocks ON files.id = blocks.fileid "
			"WHERE files.path_hmac=:path_hmac", {
					{":path_hmac", path_hmac}
			});
	for(auto row : blocks_data){
		if(!enc_storage.have_encblock(row[0])) enc_storage.put_encblock(row[0], get_encblock(row[0]));
	}

	if(delete_file){
		directory_db->exec("UPDATE openfs SET assembled=0 WHERE file_path_hmac=:path_hmac", {
				{":path_hmac", path_hmac}
		});
		fs::remove(fs::absolute(file_path, open_path));
	}
}

} /* namespace syncfs */
} /* namespace librevault */
