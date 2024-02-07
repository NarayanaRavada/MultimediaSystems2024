#include <algorithm>
#include <fstream>
#include <divsufsort.h>
#include <iostream>
#include <map>
#include <numeric>
#include <cstring>
#include <vector>

using std::ios_base;

using byte = unsigned char;

class Encoder {
    byte *input;
    int in_size;
    byte *output;
    int out_size;
    std::string outFile;

    public:
    Encoder(std::string inFile): outFile(inFile) {
        std::ifstream inBuf(inFile, ios_base::in | ios_base::binary);

        // read to input
        inBuf.seekg(0, ios_base::end);
        in_size = inBuf.tellg();
        inBuf.seekg(0, ios_base::beg);
        input = new byte[in_size];
        inBuf.read((char *)input, in_size);
        inBuf.close();
    }

    void encode() {
        byte *bwst_out = new byte[in_size];
        int pidx = divbwt((unsigned char *)input, (unsigned char*)bwst_out, NULL, in_size);

        // Dynamic byte remapping 
        std::map<int, int> freq;
        for (int i = 0; i < in_size; i++) {
            freq[bwst_out[i]]++;
        }

        std::vector<std::pair<int, int>> arr;

        for (auto [el, f]: freq) {
            arr.emplace_back(f, el);
        }

        std::sort(arr.rbegin(), arr.rend(), [] (std::pair<int, int> a, std::pair<int, int> b) {
                return a.first == b.first ? a.second > b.second : a.first < b.first;
                });

        std::map<int, int> dyn_remap;
        for (int i = 0; i < arr.size(); i++) {
            dyn_remap[arr[i].second] = i;
        }

        // Vertical byte reading
        int *vert_out = new int[in_size];
        for (int i = 0; i < in_size; i++) {
            vert_out[i] = dyn_remap[bwst_out[i]];
        }

        std::string vert_reorder = "";
        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < in_size; j++) {
                vert_reorder += ((vert_out[j] >> i) & 1) + '0';
            }
        }
        vert_reorder += '#';

        // run length encoding
        std::vector<int> encoded_arr;

        int idx = 0, cnt = 0;
        for (int j = 0; j < vert_reorder.size(); j++) {
            if (vert_reorder[j] - '0' != idx) {
                while (cnt > 255) {
                    encoded_arr.push_back(255);
                    encoded_arr.push_back(0);
                    cnt -= 255;
                }
                if (cnt > 0) {
                    encoded_arr.push_back(cnt);
                }
                cnt = 0;
                idx ^= 1;
            }
            cnt++;
        }

        out_size = encoded_arr.size();

        output = new byte[encoded_arr.size()];
        std::copy(encoded_arr.begin(), encoded_arr.end(), output);

        std::ofstream outBuf(outFile + ".run", ios_base::out | ios_base::binary);

        // out file
        int dict_size = arr.size();
        outBuf.write((char *) &dict_size, sizeof(char));
        for (int i = 0; i < dict_size; i++) {
            outBuf << (byte)arr[i].second;
        }
        outBuf.write((char *)&out_size, sizeof(int));
        outBuf.write((char *)output, out_size);
        outBuf.write((char *)&pidx, sizeof(int));
        outBuf.write((char *)&in_size, sizeof(int));
        outBuf.close();
    }
};

class Decoder {
    std::string inFile;

    public:
    Decoder(std::string inFile): inFile(inFile) {}; 
    void decode() {
        std::ifstream buf(inFile, ios_base::in | ios_base::binary);

        byte dict_size;
        buf.read((char *)&dict_size, sizeof(byte));

        std::map<int, byte> dyn_remap;
        for (int i = 0; i < dict_size; i++) {
            char ch;
            buf.read(&ch, sizeof(byte));
            dyn_remap[i] = ch;
        }

        int enc_size; 
        buf.read((char *)&enc_size, sizeof(int));

        std::vector<byte> encoded_arr;

        for (int i = 0; i < enc_size; i++) {
            byte b;
            buf.read((char *)&b, sizeof(byte));
            encoded_arr.push_back(b);
        }

        int pidx;
        buf.read((char *)&pidx, sizeof(int));
        int in_size;
        buf.read((char *)&in_size, sizeof(int));

        /*--- inverse rle ---*/
        std::string vert_reorder = "";
        for (int i = 0; i < enc_size; i++) {
            int cnt = encoded_arr[i];
            for(int j = 0; j < cnt; j++) {
                vert_reorder += (1 - i & 1) + '0';
            }
        }


        /*--- Vertical byte writing ---*/
        std::vector<byte> vert_out(in_size, 0);
        for (int i = 0; i < vert_reorder.size(); i++) {
            vert_out[i % in_size] |=  (vert_reorder[i] - '0') << (i / in_size);
        }

        /*--- inverse dynamic byte remapping ---*/
        byte *bwst_out = new byte[in_size];
        for (int i = 0; i < in_size; i++) {
            bwst_out[i] = dyn_remap[vert_out[i]];
        }

        /*--- inverse bwst ---*/
        inverse_bw_transform(bwst_out, bwst_out, NULL, in_size, pidx);
        std::ofstream outBuf(inFile.substr(0, inFile.length() - 4), ios_base::out | ios_base::binary);
        outBuf.write((char *)bwst_out, in_size);
        outBuf.close();
    }
};

void print_help() {
    printf("\u001b[1m\u001b[4mmodified rle compressor\u001b[0m \n");
    printf("usage:  -[e (encode) | d (decode)] <FILENAME>\n");
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        print_help();
        return 1;
    }

    auto file_exists = [] (const std::string& name) {
        std::ifstream f(name.c_str());
        return f.good();
    };

    std::string filename = argv[2];

    if (!file_exists(filename)) {
        printf("rlenc: no such file: %s\n", argv[2]);
        return 1;
    }

    if (!strcmp(argv[1], "-e") || !strcmp(argv[1], "--encode")) {
        if (file_exists(filename + ".run")) {
            printf("rlenc: file already exists: %s\n", (char *)(filename + ".run").c_str());
            return 1;
        }

        Encoder* enc = new Encoder(argv[2]);
        enc->encode();
    } 
    else if (!strcmp(argv[1], "-d") || !strcmp(argv[1], "--decode")) {
        std::string outfile = filename.substr(0, filename.size() - 4);
        if (file_exists(outfile)) {
            printf("rlenc: file already exists: %s\n", outfile.c_str());
            return 1;
        }

        Decoder* dec = new Decoder(argv[2]);
        dec->decode();
    } 
    else {
        print_help();
    }
}
