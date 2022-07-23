
#include <boost/iostreams/device/mapped_file.hpp>
#include <librsync.h>
#include <stdlib.h>

void whole_file_rs_api()
{
    const char *old_path = "old.bin";
    const char *sig_path = "sig.bin";
    const char *new_path = "new.bin";
    const char *diff_path = "diff.bin";
    const char *res_path = "res.bin";

    {
        FILE *old_file = fopen(old_path, "w+b");
        fwrite("old data", 1, 8, old_file);
        fclose(old_file);
        FILE *new_file = fopen(new_path, "w+b");
        fwrite("new data", 1, 8, new_file);
        fclose(new_file);
    }
    {
        FILE *old_file = fopen(old_path, "rb");
        rs_magic_number mnum = (rs_magic_number)0;
        size_t block_len = 0;
        size_t strong_len = 0;
        auto res1 = rs_sig_args(rs_file_size(old_file), &mnum, &block_len, &strong_len);

        FILE *sig_file = fopen(sig_path, "w+b");
        rs_stats stat{};
        auto res = rs_sig_file(old_file, sig_file, block_len, strong_len, mnum, &stat);
        fclose(old_file);
        fclose(sig_file);
    }
    rs_signature_t *sign;
    {
        FILE *sig_file = fopen(sig_path, "rb");

        rs_stats stat{};
        auto res = rs_loadsig_file(sig_file, &sign, &stat);
        auto res1 = rs_build_hash_table(sign);
        fclose(sig_file);
    }
    {
        FILE *new_file = fopen(new_path, "rb");
        FILE *delta_file = fopen(diff_path, "w+b");
        rs_stats stat{};
        auto res = rs_delta_file(sign, new_file, delta_file, &stat);
        fclose(new_file);
        fclose(delta_file);
    }
    {
        FILE *old_file = fopen(old_path, "rb");
        FILE *delta_file = fopen(diff_path, "rb");
        FILE *res_file = fopen(res_path, "w+b");
        rs_stats stat{};
        auto res = rs_patch_file(old_file, delta_file, res_file, &stat);
        fclose(old_file);
        fclose(delta_file);
        fclose(res_file);
    }
}

void stream_rs_api()
{
#ifdef _WIN32
    fs::path::imbue(std::locale(std::locale(), new std::codecvt_utf8_utf16<wchar_t>()));
#endif
    namespace bio = boost::iostreams;
    {
        FILE *old_file = fopen("old.txt", "w+b");
        fwrite("old data", 1, 8, old_file);
        fclose(old_file);
        FILE *new_file = fopen("new.txt", "w+b");
        fwrite("new data", 1, 8, new_file);
        fclose(new_file);
    } // namespace boost::iostreams;

    auto old_path = fs::path("old.bin");
    auto new_path = fs::path("new.bin");
    auto res_path = fs::path("res.bin");

    // stream emulator
    std::list<std::string> sign_data;

    size_t sign_size = 0;
    size_t block_len = 0;
    size_t strong_len = 0;
    // Create Sing old file
    {
        bio::mapped_file f;
        if (fs::exists(old_path))
        {
            f.open(old_path, bio::mapped_file::readonly);
        }
        if (!f.is_open())
        {
            return;
        }

        rs_magic_number sig_magic = (rs_magic_number)0;

        auto res1 = rs_sig_args(f.size(), &sig_magic, &block_len, &strong_len);
        auto job = rs_sig_begin(block_len, strong_len, sig_magic);

        std::string chunk;
        /* recomended size outbuf: header + 4 blocksums. */
        // chunk.resize(12 + 4 * (4 + strong_len))
        rs_buffers_t buf;
        buf.avail_in = f.size();
        buf.next_in = (char *)f.const_data();
        buf.eof_in = 1;
        buf.avail_out = chunk.size();
        buf.next_out = (char *)chunk.data();
        rs_result res;
        while ((res = rs_job_iter(job, &buf)) == RS_BLOCKED)
        {
            sign_size += chunk.size() - buf.avail_out;
            if (buf.avail_out)
            {
                chunk.resize(chunk.size() - buf.avail_out);
            }
            if (chunk.size())
            {
                sign_data.emplace_back(std::move(chunk));
            }
            chunk.resize(4096);
            buf.avail_out = chunk.size();
            buf.next_out = (char *)chunk.data();
        }
        if (res != RS_DONE)
        {
            return;
        }
        if (buf.avail_out)
        {
            chunk.resize(chunk.size() - buf.avail_out);
        }
        if (chunk.size())
        {
            sign_data.emplace_back(std::move(chunk));
        }
        printf("Sign size: %llu", sign_size);
        /* recomended size */
        /* Size inbuf for 4 blocks, outbuf for header + 4 blocksums. */
        /*       rs_job_iter(job, &buf);
           auto r = rs_whole_run(job, old_file, sig_file, 4 * (int)block_len, 12 + 4 * (4 + (int)strong_len)); */
        rs_job_free(job);
    }

    rs_signature_t *sign;
    {
        auto job = rs_loadsig_begin(&sign);
        rs_result res = RS_BLOCKED;
        rs_buffers_t buf;
        buf.avail_in = sign_data.front().size();
        buf.next_in = (char *)sign_data.front().data();
        buf.eof_in = 0;
        buf.avail_out = 0;
        buf.next_out = nullptr;
        while ((res = rs_job_iter(job, &buf)) == RS_BLOCKED)
        {
            sign_data.pop_front();
            if ((buf.eof_in = sign_data.empty()) == 0)
            {
                buf.avail_in = sign_data.front().size();
                buf.next_in = (char *)sign_data.front().data();
            }
        }
        if (res != RS_DONE)
        {
            printf("Error load sign\n");
            return;
        }
        if (rs_build_hash_table(sign) != RS_DONE)
        {
            return;
        }
    }
    // streem emulator
    std::list<std::string> delta_data;
    size_t delta_size = 0;
    {
        bio::mapped_file f;
        if (fs::exists(new_path))
        {
            f.open(new_path, bio::mapped_file::readonly);
        }
        if (!f.is_open())
        {
            return;
        }
        auto job = rs_delta_begin(sign);
        rs_result res = RS_BLOCKED;

        size_t readed_count = std::min(block_len, f.size());
        /*
         * The size of the data chunk must be no larger than 2*block_len in the signature,
         * otherwise librsync will allocate a lot of memory
         */
        rs_buffers_t buf;
        buf.avail_in = block_len;
        buf.next_in = (char *)f.const_data();
        buf.eof_in = 0;
        buf.avail_out = 0;
        buf.next_out = nullptr;
        std::string chunk;
        while ((res = rs_job_iter(job, &buf)) == RS_BLOCKED)
        {
            if (buf.avail_in == 0)
            {
                if ((buf.eof_in = readed_count >= f.size()) == 0)
                {
                    buf.avail_in = std::min(block_len, f.size() - readed_count);
                    readed_count += buf.avail_in;
                }
            }
            if (buf.avail_out == 0)
            {
                delta_size += chunk.size() - buf.avail_out;
                if (chunk.size())
                {
                    delta_data.emplace_back(std::move(chunk));
                }
                chunk.resize(block_len);
                buf.avail_out = chunk.size();
                buf.next_out = (char *)chunk.data();
            }
        }
        if (res != RS_DONE)
        {
            return;
        }
        if (buf.avail_out)
        {
            chunk.resize(chunk.size() - buf.avail_out);
        }
        if (chunk.size())
        {
            delta_data.emplace_back(std::move(chunk));
        }
    }
    {
        bio::mapped_file old;
        if (fs::exists(old_path))
        {
            old.open(old_path, bio::mapped_file::readonly);
        }
        if (!old.is_open())
        {
            return;
        }
        bio::mapped_file_params params;
        params.path = res_path.string();
        params.new_file_size = fs::file_size(new_path);
        params.mode = (std::ios_base::out | std::ios_base::in);
        bio::mapped_file res;
        res.open(params); // res_path, bio::mapped_file::readwrite);
        if (!res.is_open())
        {
            return;
        }
        rs_copy_cb *cb = [](void *arg, rs_long_t pos, size_t *len, void **buf) -> rs_result
        {
            char *ptr = (char *)arg;
            auto ret = memcpy(*buf, &ptr[pos], *len);
            return RS_DONE;
            /*if (fseek(f, pos, SEEK_SET)) {
                rs_error("seek failed: %s", strerror(errno));
                return RS_IO_ERROR;
            }
            *len = fread(*buf, 1, *len, f);
            if (*len) {
                return RS_DONE;
            } else if (ferror(f)) {
                rs_error("read error: %s", strerror(errno));
                return RS_IO_ERROR;
            } else {
                rs_error("unexpected eof on fd%d", fileno(f));
                return RS_INPUT_ENDED;
            }*/
        };
        auto job = rs_patch_begin(cb, (char *)old.const_data());
        rs_buffers_t buf;
        buf.avail_in = delta_data.front().size();
        buf.next_in = (char *)delta_data.front().data();
        buf.eof_in = 0;
        buf.avail_out = res.size();
        buf.next_out = res.data();
        rs_result rc;
        while ((rc = rs_job_iter(job, &buf)) == RS_BLOCKED)
        {
            if (buf.avail_out == 0)
            {
                int wtf = 1;
            }
            delta_data.pop_front();
            if ((buf.eof_in = delta_data.empty()) == 0)
            {
                buf.avail_in = delta_data.front().size();
                buf.next_in = (char *)delta_data.front().data();
            }
        }
        res.close();
    }
}