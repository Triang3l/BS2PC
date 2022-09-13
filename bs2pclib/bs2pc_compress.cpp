#include "bs2pclib.hpp"

#include "../zlib/zlib.h"

#include <cstdint>
#include <cstring>

namespace bs2pc {

bool is_gbx_map_compressed(void const * const map_file, size_t const map_file_size) {
	if (map_file_size < sizeof(uint32_t) + 2) {
		return false;
	}
	char const * const map_bytes = reinterpret_cast<char const *>(map_file);
	return uint8_t(map_bytes[sizeof(uint32_t)]) == gbx_map_zlib_cmf &&
			uint8_t(map_bytes[sizeof(uint32_t) + 1]) == gbx_map_zlib_flg;
}

bool compress_gbx_map(void const * const uncompressed, size_t const uncompressed_size, std::vector<char> & compressed) {
	if (uncompressed_size > UINT32_MAX) {
		// Gearbox map files store a 32-bit uncompressed size.
		return false;
	}
	// Make sure the uncompressed size can be used as all types it's used as.
	if (uncompressed_size != uInt(uncompressed_size) || uncompressed_size != uLong(uncompressed_size)) {
		return false;
	}
	z_stream stream;
	stream.zalloc = nullptr;
	stream.zfree = nullptr;
	stream.opaque = nullptr;
	if (deflateInit2(&stream, gbx_map_zlib_level, Z_DEFLATED, gbx_map_zlib_window_bits, 8, Z_DEFAULT_STRATEGY) !=
			Z_OK) {
		return false;
	}
	stream.next_in = const_cast<Bytef z_const *>(reinterpret_cast<Bytef const *>(uncompressed));
	stream.avail_in = uInt(uncompressed_size);
	uLong const deflate_bound = deflateBound(&stream, uLong(uncompressed_size));
	// Make sure the upper bound can be used as all types it's used as,
	// and also that both the uncompressed size and the compressed data can be stored in a vector.
	if (deflate_bound != size_t(deflate_bound) || SIZE_MAX - size_t(deflate_bound) < sizeof(uint32_t) ||
			deflate_bound != uInt(deflate_bound)) {
		deflateEnd(&stream);
		return false;
	}
	// Make sure any potential unwritten bytes are zero for deterministic conversion.
	compressed.clear();
	compressed.resize(sizeof(uint32_t) + size_t(deflate_bound));
	stream.next_out = reinterpret_cast<Bytef *>(compressed.data() + sizeof(uint32_t));
	stream.avail_out = uInt(deflate_bound);
	int const deflate_result = deflate(&stream, Z_FINISH);
	deflateEnd(&stream);
	if (deflate_result != Z_STREAM_END) {
		return false;
	}
	// Truncate the output since the upper bound may be larger than the compressed size.
	if (stream.avail_out > deflate_bound) {
		return false;
	}
	compressed.resize(compressed.size() - stream.avail_out);
	// Gearbox maps store the compressed size in the beginning.
	uint32_t uncompressed_size_32 = uint32_t(uncompressed_size);
	std::memcpy(compressed.data(), &uncompressed_size_32, sizeof(uint32_t));
	return true;
}

bool decompress_gbx_map(void const * const compressed, size_t const compressed_size, std::vector<char> & uncompressed) {
	if (compressed_size < sizeof(uint32_t)) {
		// The uncompressed size is out of bounds.
		return false;
	}
	size_t const compressed_stream_size = compressed_size - sizeof(uint32_t);
	// Make sure the compressed size can be used as all types it's used as.
	if (compressed_stream_size != uInt(compressed_stream_size)) {
		return false;
	}
	uint32_t uncompressed_size_32;
	std::memcpy(&uncompressed_size_32, compressed, sizeof(uint32_t));
	// Make sure the uncompressed size can be used as all types it's used as.
	if (uncompressed_size_32 != size_t(uncompressed_size_32) || uncompressed_size_32 != uInt(uncompressed_size_32)) {
		return false;
	}
	z_stream stream;
	stream.next_in = const_cast<Bytef z_const *>(reinterpret_cast<Bytef const *>(compressed) + sizeof(uint32_t));
	stream.avail_in = uInt(compressed_stream_size);
	stream.zalloc = nullptr;
	stream.zfree = nullptr;
	stream.opaque = nullptr;
	if (inflateInit(&stream) != Z_OK) {
		return false;
	}
	// Make sure any potential unwritten bytes are zero for deterministic conversion,
	// though the size shouldn't be different than the actual compressed data size, but the size is stored externally.
	uncompressed.clear();
	uncompressed.resize(size_t(uncompressed_size_32));
	stream.next_out = reinterpret_cast<Bytef *>(uncompressed.data());
	stream.avail_out = uInt(uncompressed_size_32);
	int const inflate_result = inflate(&stream, Z_FINISH);
	inflateEnd(&stream);
	return inflate_result == Z_STREAM_END;
}

}
