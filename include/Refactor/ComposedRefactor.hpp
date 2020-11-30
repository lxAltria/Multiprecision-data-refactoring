#ifndef _MDR_COMPOSED_REFACTOR_HPP
#define _MDR_COMPOSED_REFACTOR_HPP

#include "RefactorInterface.hpp"
#include "Decomposer/Decomposer.hpp"
#include "Interleaver/Interleaver.hpp"
#include "BitplaneEncoder/BitplaneEncoder.hpp"
#include "ErrorCollector/ErrorCollector.hpp"
#include "LosslessCompressor/LosslessCompressor.hpp"
#include "Writer/Writer.hpp"
#include "RefactorUtils.hpp"

namespace MDR {
    // a decomposition-based scientific data refactor: compose a refactor using decomposer, interleaver, encoder, and error collector
    template<class T, class Decomposer, class Interleaver, class Encoder, class ErrorCollector, class Writer>
    class ComposedRefactor : public concepts::RefactorInterface<T> {
    public:
        ComposedRefactor(Decomposer decomposer, Interleaver interleaver, Encoder encoder, ErrorCollector collector, Writer writer)
            : decomposer(decomposer), interleaver(interleaver), encoder(encoder), collector(collector), writer(writer) {}

        void refactor(T const * data_, const std::vector<uint32_t>& dims, uint8_t target_level, uint8_t num_bitplanes){
            dimensions = dims;
            uint32_t num_elements = 1;
            for(const auto& dim:dimensions){
                num_elements *= dim;
            }
            data = std::vector<T>(data_, data_ + num_elements);
            // if refactor successfully
            if(refactor(target_level, num_bitplanes)){
                writer.write_level_components(level_components, level_sizes);
            }
            for(int i=0; i<level_components.size(); i++){
                for(int j=0; j<level_components[i].size(); j++){
                    free(level_components[i][j]);                    
                }
            }
        }

        void write_metadata() const {
            uint32_t metadata_size = sizeof(uint8_t) + get_size(dimensions) // dimensions
                            + sizeof(uint8_t) + get_size(level_error_bounds) + get_size(level_squared_errors) + get_size(level_sizes); // level information
            uint8_t * metadata = (uint8_t *) malloc(metadata_size);
            uint8_t * metadata_pos = metadata;
            *(metadata_pos ++) = (uint8_t) dimensions.size();
            serialize(dimensions, metadata_pos);
            *(metadata_pos ++) = (uint8_t) level_error_bounds.size();
            serialize(level_error_bounds, metadata_pos);
            serialize(level_squared_errors, metadata_pos);
            serialize(level_sizes, metadata_pos);
            writer.write_metadata(metadata, metadata_size);
            free(metadata);
        }

        ~ComposedRefactor(){}

        void print() const {
            std::cout << "Composed refactor with the following components." << std::endl;
            std::cout << "Decomposer: "; decomposer.print();
            std::cout << "Interleaver: "; interleaver.print();
            std::cout << "Encoder: "; encoder.print();
        }
    private:
        bool refactor(uint8_t target_level, uint8_t num_bitplanes){
            uint8_t max_level = log2(*min_element(dimensions.begin(), dimensions.end())) - 1;
            if(target_level > max_level){
                std::cerr << "Target level is higher than " << max_level << std::endl;
                return false;
            }
            // decompose data hierarchically
            decomposer.decompose(data.data(), dimensions, target_level);

            auto lossless_compressor = ZSTD();
            // encode level by level
            level_error_bounds.clear();
            level_squared_errors.clear();
            level_components.clear();
            level_sizes.clear();
            auto level_dims = compute_level_dims(dimensions, target_level);
            auto level_elements = compute_level_elements(level_dims, target_level);
            std::vector<uint32_t> dims_dummy(dimensions.size(), 0);
            SquaredErrorCollector<T> s_collector = SquaredErrorCollector<T>();
            for(int i=0; i<=target_level; i++){
                const std::vector<uint32_t>& prev_dims = (i == 0) ? dims_dummy : level_dims[i - 1];
                T * buffer = (T *) malloc(level_elements[i] * sizeof(T));
                // extract level i component
                interleaver.interleave(data.data(), dimensions, level_dims[i], prev_dims, reinterpret_cast<T*>(buffer));
                // compute max coefficient as level error bound
                T level_max_error = compute_max_abs_value(reinterpret_cast<T*>(buffer), level_elements[i]);
                level_error_bounds.push_back(level_max_error);
                // collect errors
                auto collected_error = s_collector.collect_level_error(buffer, level_elements[i], num_bitplanes, level_max_error);
                level_squared_errors.push_back(collected_error);
                // encode level data
                int level_exp = 0;
                frexp(level_max_error, &level_exp);
                std::vector<uint32_t> stream_sizes;
                auto streams = encoder.encode(buffer, level_elements[i], level_exp, num_bitplanes, stream_sizes);
                free(buffer);
                // Optional lossless compression
                for(int j=0; j<streams.size(); j++){
                    uint8_t * compressed = NULL;
                    auto compressed_size = lossless_compressor.compress(streams[j], stream_sizes[j], &compressed);
                    if(compressed != streams[j]){
                        free(streams[j]);
                        streams[j] = compressed;
                        stream_sizes[j] = compressed_size;
                    }
                }
                // record encoded level data and size
                level_components.push_back(streams);
                level_sizes.push_back(stream_sizes);
            }
            return true;
        }

        Decomposer decomposer;
        Interleaver interleaver;
        Encoder encoder;
        ErrorCollector collector;
        Writer writer;
        std::vector<T> data;
        std::vector<uint32_t> dimensions;
        std::vector<T> level_error_bounds;
        std::vector<std::vector<uint8_t*>> level_components;
        std::vector<std::vector<uint32_t>> level_sizes;
        std::vector<std::vector<double>> level_squared_errors;
    };
}
#endif

