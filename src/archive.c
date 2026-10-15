#include "archive.h"
#include "tinf.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Forward declaration of search_buffer from engine.c
void engine_search_decompressed_buffer(
    const SearchEngineContext* ctx,
    const SearchOptions* options,
    const char* data,
    size_t data_len,
    const char* file_path,
    ThreadResult* result,
    int* file_match_count,
    volatile LONG* global_match_count,
    volatile LONG* stop_flag
);

static inline uint16_t read_u16_le(const unsigned char* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static inline uint32_t read_u32_le(const unsigned char* p) {
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

bool archive_is_supported_file(const char* file_path, const unsigned char* buffer, size_t length) {
    if (!file_path) return false;

    // Check magic signature first if buffer is available
    if (buffer && length >= 4) {
        // Zip local header: PK\x03\x04
        if (buffer[0] == 0x50 && buffer[1] == 0x4b && buffer[2] == 0x03 && buffer[3] == 0x04) return true;
        // Zip empty archive / central directory: PK\x05\x06 or PK\x01\x02
        if (buffer[0] == 0x50 && buffer[1] == 0x4b && (buffer[2] == 0x05 || buffer[2] == 0x01)) return true;
        // Gzip header: \x1f\x8b
        if (buffer[0] == 0x1f && buffer[1] == 0x8b) return true;
    }

    // Check file extension
    size_t l = strlen(file_path);
    if (l >= 4 && _stricmp(&file_path[l - 4], ".zip") == 0) return true;
    if (l >= 3 && _stricmp(&file_path[l - 3], ".gz") == 0) return true;
    if (l >= 4 && _stricmp(&file_path[l - 4], ".tgz") == 0) return true;

    return false;
}

// Search inside a Gzip compressed buffer (.gz, .log.gz, .txt.gz)
static bool search_gzip_buffer(
    const SearchEngineContext* ctx,
    const SearchOptions* options,
    const char* archive_path,
    const unsigned char* buffer,
    size_t length,
    ThreadResult* result,
    int* file_match_count,
    volatile LONG* global_match_count,
    volatile LONG* stop_flag
) {
    if (!buffer || length < 18) return false; // Minimum gzip header + footer
    if (buffer[0] != 0x1f || buffer[1] != 0x8b) return false;

    // Last 4 bytes of gzip stream contain original uncompressed size modulo 2^32
    uint32_t isize = read_u32_le(&buffer[length - 4]);
    unsigned int dest_cap = isize;
    if (dest_cap == 0 || dest_cap > 512 * 1024 * 1024) {
        dest_cap = (unsigned int)(length * 5 + 65536);
    }

    unsigned char* dest = (unsigned char*)safe_malloc(dest_cap + 1);
    unsigned int dest_len = dest_cap;

    int res = tinf_gzip_uncompress(dest, &dest_len, buffer, (unsigned int)length);
    if (res == TINF_BUF_ERROR) {
        // Expand buffer and retry
        free(dest);
        dest_cap *= 4;
        dest = (unsigned char*)safe_malloc(dest_cap + 1);
        dest_len = dest_cap;
        res = tinf_gzip_uncompress(dest, &dest_len, buffer, (unsigned int)length);
    }

    if (res != TINF_OK) {
        free(dest);
        return false;
    }

    dest[dest_len] = '\0';

    // Construct clean virtual path: e.g. "syslog.log.gz"
    engine_search_decompressed_buffer(ctx, options, (const char*)dest, dest_len,
                                      archive_path, result, file_match_count,
                                      global_match_count, stop_flag);

    free(dest);
    return true;
}

// Search inside a ZIP archive (.zip)
static bool search_zip_buffer(
    const SearchEngineContext* ctx,
    const SearchOptions* options,
    const char* archive_path,
    const unsigned char* buffer,
    size_t length,
    ThreadResult* result,
    int* file_match_count,
    volatile LONG* global_match_count,
    volatile LONG* stop_flag
) {
    if (!buffer || length < 22) return false;

    size_t offset = 0;
    bool found_any_entry = false;

    // 1. Iterate through Local File Headers (0x04034b50)
    while (offset + 30 <= length) {
        if (stop_flag && *stop_flag) return true;

        uint32_t sig = read_u32_le(&buffer[offset]);
        if (sig != 0x04034b50) {
            // Reached Central Directory or End of Entries
            break;
        }

        uint16_t flags = read_u16_le(&buffer[offset + 6]);
        uint16_t method = read_u16_le(&buffer[offset + 8]);
        uint32_t comp_size = read_u32_le(&buffer[offset + 18]);
        uint32_t uncomp_size = read_u32_le(&buffer[offset + 22]);
        uint16_t filename_len = read_u16_le(&buffer[offset + 26]);
        uint16_t extra_len = read_u16_le(&buffer[offset + 28]);

        size_t data_offset = offset + 30 + filename_len + extra_len;
        if (data_offset > length) break;

        char entry_name[MAX_PATH];
        size_t copy_len = (filename_len < sizeof(entry_name) - 1) ? filename_len : (sizeof(entry_name) - 1);
        memcpy(entry_name, &buffer[offset + 30], copy_len);
        entry_name[copy_len] = '\0';

        // Check for streaming data descriptor flag (bit 3: sizes zeroed in local header)
        if ((flags & 0x08) != 0 && comp_size == 0) {
            // Defer to Central Directory pass
            break;
        }

        if (data_offset + comp_size > length) break;
        found_any_entry = true;

        // Skip directory entries (ending with / or \)
        if (copy_len > 0 && entry_name[copy_len - 1] != '/' && entry_name[copy_len - 1] != '\\') {
            // Check extension filter
            bool ext_ok = true;
            if (*options->extensions && !file_matches_extension(entry_name, options->extensions)) {
                ext_ok = false;
            }

            if (ext_ok) {
                char virtual_path[MAX_PATH * 2];
                snprintf(virtual_path, sizeof(virtual_path), "%s::%s", archive_path, entry_name);

                if (method == 0) {
                    // Stored: zero-copy direct memory search
                    engine_search_decompressed_buffer(ctx, options, (const char*)&buffer[data_offset], comp_size,
                                                      virtual_path, result, file_match_count,
                                                      global_match_count, stop_flag);
                } else if (method == 8) {
                    // Deflated: decompress with tinf
                    if (uncomp_size > 0 && uncomp_size <= 256 * 1024 * 1024) {
                        unsigned char* uncomp = (unsigned char*)safe_malloc(uncomp_size + 1);
                        unsigned int dlen = uncomp_size;
                        int res = tinf_uncompress(uncomp, &dlen, &buffer[data_offset], comp_size);
                        if (res == TINF_OK) {
                            uncomp[dlen] = '\0';
                            engine_search_decompressed_buffer(ctx, options, (const char*)uncomp, dlen,
                                                              virtual_path, result, file_match_count,
                                                              global_match_count, stop_flag);
                        }
                        free(uncomp);
                    }
                }
            }
        }

        offset = data_offset + comp_size;
    }

    // 2. Fallback to Central Directory if streaming zip or no entries processed from local headers
    if (!found_any_entry && length >= 22) {
        // Find End of Central Directory record (0x06054b50) scanning backwards from EOF
        size_t max_search = (length > 65557) ? 65557 : length;
        size_t eocd_pos = 0;
        bool found_eocd = false;

        for (size_t i = length - 22; i >= length - max_search; i--) {
            if (read_u32_le(&buffer[i]) == 0x06054b50) {
                eocd_pos = i;
                found_eocd = true;
                break;
            }
            if (i == 0) break;
        }

        if (found_eocd) {
            uint32_t cd_size = read_u32_le(&buffer[eocd_pos + 12]);
            uint32_t cd_offset = read_u32_le(&buffer[eocd_pos + 16]);

            if (cd_offset + cd_size <= length) {
                size_t cd_cur = cd_offset;
                while (cd_cur + 46 <= cd_offset + cd_size) {
                    if (stop_flag && *stop_flag) return true;

                    if (read_u32_le(&buffer[cd_cur]) != 0x02014b50) break;

                    uint16_t method = read_u16_le(&buffer[cd_cur + 10]);
                    uint32_t comp_size = read_u32_le(&buffer[cd_cur + 20]);
                    uint32_t uncomp_size = read_u32_le(&buffer[cd_cur + 24]);
                    uint16_t filename_len = read_u16_le(&buffer[cd_cur + 28]);
                    uint16_t extra_len = read_u16_le(&buffer[cd_cur + 30]);
                    uint16_t comment_len = read_u16_le(&buffer[cd_cur + 32]);
                    uint32_t local_header_offset = read_u32_le(&buffer[cd_cur + 42]);

                    char entry_name[MAX_PATH];
                    size_t copy_len = (filename_len < sizeof(entry_name) - 1) ? filename_len : (sizeof(entry_name) - 1);
                    memcpy(entry_name, &buffer[cd_cur + 46], copy_len);
                    entry_name[copy_len] = '\0';

                    if (copy_len > 0 && entry_name[copy_len - 1] != '/' && entry_name[copy_len - 1] != '\\') {
                        bool ext_ok = true;
                        if (*options->extensions && !file_matches_extension(entry_name, options->extensions)) {
                            ext_ok = false;
                        }

                        if (ext_ok && local_header_offset + 30 <= length) {
                            uint16_t l_flen = read_u16_le(&buffer[local_header_offset + 26]);
                            uint16_t l_elen = read_u16_le(&buffer[local_header_offset + 28]);
                            size_t data_offset = local_header_offset + 30 + l_flen + l_elen;

                            if (data_offset + comp_size <= length) {
                                char virtual_path[MAX_PATH * 2];
                                snprintf(virtual_path, sizeof(virtual_path), "%s::%s", archive_path, entry_name);

                                if (method == 0) {
                                    engine_search_decompressed_buffer(ctx, options, (const char*)&buffer[data_offset], comp_size,
                                                                      virtual_path, result, file_match_count,
                                                                      global_match_count, stop_flag);
                                } else if (method == 8 && uncomp_size > 0 && uncomp_size <= 256 * 1024 * 1024) {
                                    unsigned char* uncomp = (unsigned char*)safe_malloc(uncomp_size + 1);
                                    unsigned int dlen = uncomp_size;
                                    int res = tinf_uncompress(uncomp, &dlen, &buffer[data_offset], comp_size);
                                    if (res == TINF_OK) {
                                        uncomp[dlen] = '\0';
                                        engine_search_decompressed_buffer(ctx, options, (const char*)uncomp, dlen,
                                                                          virtual_path, result, file_match_count,
                                                                          global_match_count, stop_flag);
                                    }
                                    free(uncomp);
                                }
                            }
                        }
                    }

                    cd_cur += 46 + filename_len + extra_len + comment_len;
                }
            }
        }
    }

    return true;
}

bool archive_search_buffer(
    const SearchEngineContext* ctx,
    const SearchOptions* options,
    const char* archive_path,
    const unsigned char* buffer,
    size_t length,
    ThreadResult* result,
    int* file_match_count,
    volatile LONG* global_match_count,
    volatile LONG* stop_flag
) {
    if (!buffer || length == 0) return false;

    // Check gzip signature
    if (length >= 2 && buffer[0] == 0x1f && buffer[1] == 0x8b) {
        return search_gzip_buffer(ctx, options, archive_path, buffer, length, result,
                                  file_match_count, global_match_count, stop_flag);
    }

    // Check zip signature
    if (length >= 4 && buffer[0] == 0x50 && buffer[1] == 0x4b) {
        return search_zip_buffer(ctx, options, archive_path, buffer, length, result,
                                 file_match_count, global_match_count, stop_flag);
    }

    // Check file extensions fallback
    size_t l = strlen(archive_path);
    if (l >= 3 && _stricmp(&archive_path[l - 3], ".gz") == 0) {
        return search_gzip_buffer(ctx, options, archive_path, buffer, length, result,
                                  file_match_count, global_match_count, stop_flag);
    }
    if (l >= 4 && _stricmp(&archive_path[l - 4], ".zip") == 0) {
        return search_zip_buffer(ctx, options, archive_path, buffer, length, result,
                                 file_match_count, global_match_count, stop_flag);
    }

    return false;
}
