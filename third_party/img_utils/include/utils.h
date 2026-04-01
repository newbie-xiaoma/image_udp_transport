/*************************************************************************************************************************
 * Copyright 2024 Grifcc
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the “Software”), to deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *************************************************************************************************************************/
#pragma once

#include <cstring>
#include <fstream>
#include <memory>
#include <chrono>

std::shared_ptr<unsigned char> getTimestamp()
{
    auto epoch = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now()).time_since_epoch();
    long long unix_timestamp = std::chrono::duration_cast<std::chrono::seconds>(epoch).count();
    unsigned char *timestamp_buf = new unsigned char[8];

    timestamp_buf[0] = static_cast<unsigned char>(unix_timestamp >> 56 & 0xff);
    timestamp_buf[1] = static_cast<unsigned char>(unix_timestamp >> 48 & 0xff);
    timestamp_buf[2] = static_cast<unsigned char>(unix_timestamp >> 40 & 0xff);
    timestamp_buf[3] = static_cast<unsigned char>(unix_timestamp >> 32 & 0xff);
    timestamp_buf[4] = static_cast<unsigned char>(unix_timestamp >> 24 & 0xff);
    timestamp_buf[5] = static_cast<unsigned char>(unix_timestamp >> 16 & 0xff);
    timestamp_buf[6] = static_cast<unsigned char>(unix_timestamp >> 8 & 0xff);
    timestamp_buf[7] = static_cast<unsigned char>(unix_timestamp & 0xff);

    return std::shared_ptr<unsigned char>(timestamp_buf, [](unsigned char *ptr)
                                          { delete[] ptr; });
}

long long Stream2Timestamp(const unsigned char *data)
{
    return (static_cast<long long>(data[0]) << 56) |
           (static_cast<long long>(data[1]) << 48) |
           (static_cast<long long>(data[2]) << 40) |
           (static_cast<long long>(data[3]) << 32) |
           (static_cast<long long>(data[4]) << 24) |
           (static_cast<long long>(data[5]) << 16) |
           (static_cast<long long>(data[6]) << 8) | data[7];
}


bool wirte_file(const std::string &path, void *data, size_t len)
{
    FILE *file = fopen(path.c_str(), "wb");
    if (file == nullptr)
    {
        fprintf(stderr, "can't open file: %s\n", path.c_str());
        return false;
    }
    fwrite(data, sizeof(unsigned char), len, file);
    fclose(file);
    return true;
}

bool read_file(const std::string &path, void **data, int &len)
{
    FILE *file = fopen(path.c_str(), "rb");
    if (file == nullptr)
    {
        fprintf(stderr, "can't open file: %s\n", path.c_str());
        return false;
    }
    fseek(file, 0, SEEK_END);
    len = ftell(file);
    fseek(file, 0, SEEK_SET);

    *data = realloc(*data, len);
    fread(*data, len, 1, file);
    fclose(file);
    return true;
}

unsigned short fromBigEndian2Ushort(const unsigned char *data)
{
    return (static_cast<unsigned short>(data[0]) << 8) | data[1];
}

unsigned int fromBigEndian2Uint(const unsigned char *data)
{
    return (static_cast<unsigned int>(data[0]) << 24) | (static_cast<unsigned int>(data[1]) << 16) | (static_cast<unsigned int>(data[2]) << 8) | data[3];
}

unsigned char cal_sum(const unsigned char *data, size_t len)
{
    unsigned char sum = 0;
    for (int i = 0; i < len; ++i)
    {
        sum = (sum + data[i]) & 0xff;
    }
    return sum;
}