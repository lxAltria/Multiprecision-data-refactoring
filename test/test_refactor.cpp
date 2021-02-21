#include <iostream>
#include <ctime>
#include <cstdlib>
#include <vector>
#include <iomanip>
#include <cmath>
#include <bitset>
#include "utils.hpp"
#include "Refactor/Refactor.hpp"

using namespace std;

template <class T, class Refactor>
void evaluate(const vector<T>& data, const vector<uint32_t>& dims, int target_level, int num_bitplanes, Refactor refactor){
    T max_v = data[0];
    T min_v = data[0];
    for(int i=1; i<data.size(); i++){
        if(data[i] < min_v) min_v = data[i];
        if(data[i] > max_v) max_v = data[i];
    }
    T value_range = max_v - min_v;
    struct timespec start, end;
    int err = 0;
    cout << "Start refactoring" << endl;
    err = clock_gettime(CLOCK_REALTIME, &start);
    refactor.refactor(data.data(), dims, target_level, num_bitplanes);
    err = clock_gettime(CLOCK_REALTIME, &end);
    cout << "Refactor time: " << (double)(end.tv_sec - start.tv_sec) + (double)(end.tv_nsec - start.tv_nsec)/(double)1000000000 << "s" << endl;

    uint32_t data_size = 0;
    vector<int> positions;
    uint8_t * refactored_data = refactor.get_data(value_range, positions, data_size);
    for(int i=0; i<positions.size(); i++){
        cout << positions[i] << " ";
    }
    cout << endl;
    cout << "data_size = " << data_size << endl;
    uint32_t metadata_size = 0;
    uint8_t * metadata = refactor.write_metadata(metadata_size);
    cout << "metadata_size = " << metadata_size << endl;
    free(refactored_data);
    free(metadata);

}

template <class T, class Decomposer, class Interleaver, class Encoder, class Compressor, class ErrorCollector, class Writer>
void test(string filename, const vector<uint32_t>& dims, int target_level, int num_bitplanes, Decomposer decomposer, Interleaver interleaver, Encoder encoder, Compressor compressor, ErrorCollector collector, Writer writer){
    auto refactor = MDR::ComposedRefactor<T, Decomposer, Interleaver, Encoder, Compressor, ErrorCollector, Writer>(decomposer, interleaver, encoder, compressor, collector, writer);
    size_t num_elements = 0;
    auto data = MGARD::readfile<T>(filename.c_str(), num_elements);
    evaluate(data, dims, target_level, num_bitplanes, refactor);
}

int main(int argc, char ** argv){

    int argv_id = 1;
    string filename = string(argv[argv_id ++]);
    int target_level = atoi(argv[argv_id ++]);
    int num_bitplanes = atoi(argv[argv_id ++]);
    int num_dims = atoi(argv[argv_id ++]);
    vector<uint32_t> dims(num_dims, 0);
    for(int i=0; i<num_dims; i++){
        dims[i] = atoi(argv[argv_id ++]);
    }

    string metadata_file = "refactored_data/metadata.bin";
    vector<string> files;
    for(int i=0; i<=target_level; i++){
        string filename = "refactored_data/level_" + to_string(i) + ".bin";
        files.push_back(filename);
    }
    using T = float;
    using T_stream = uint32_t;
    // auto decomposer = MDR::MGARDOrthoganalDecomposer<T>();
    auto decomposer = MDR::MGARDHierarchicalDecomposer<T>();
    // auto interleaver = MDR::DirectInterleaver<T>();
    auto interleaver = MDR::SFCInterleaver<T>();
    // auto encoder = MDR::GroupedBPEncoder<T, T_stream>();
    auto encoder = MDR::PerBitBPEncoder<T, T_stream>();
    auto compressor = MDR::DefaultLevelCompressor();
    // auto compressor = MDR::NullLevelCompressor();
    auto collector = MDR::SquaredErrorCollector<T>();
    // auto collector = MDR::MaxErrorCollector<T>();
    auto writer = MDR::ConcatLevelFileWriter(metadata_file, files);
    test<T>(filename, dims, target_level, num_bitplanes, decomposer, interleaver, encoder, compressor, collector, writer);
    return 0;
}