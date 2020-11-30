#ifndef _MDR_COMPOSED_RECONSTRUCTOR_HPP
#define _MDR_COMPOSED_RECONSTRUCTOR_HPP

#include "ReconstructorInterface.hpp"
#include "Decomposer/Decomposer.hpp"
#include "Interleaver/Interleaver.hpp"
#include "BitplaneEncoder/BitplaneEncoder.hpp"
#include "Retriever/Retriever.hpp"
#include "ErrorEstimator/ErrorEstimator.hpp"
#include "SizeInterpreter/SizeInterpreter.hpp"
#include "LosslessCompressor/LosslessCompressor.hpp"
#include "RefactorUtils.hpp"

namespace MDR {
    // a decomposition-based scientific data reconstructor: inverse operator of composed refactor
    template<class T, class Decomposer, class Interleaver, class Encoder, class SizeInterpreter, class Retriever>
    class ComposedReconstructor : public concepts::ReconstructorInterface<T> {
    public:
        ComposedReconstructor(Decomposer decomposer, Interleaver interleaver, Encoder encoder, SizeInterpreter interpreter, Retriever retriever)
            : decomposer(decomposer), interleaver(interleaver), encoder(encoder), interpreter(interpreter), retriever(retriever){}

        T * reconstruct(double tolerance){
            level_num_bitplanes.clear();
            auto retrieve_sizes = interpreter.interpret_retrieve_size(level_sizes, level_errors, tolerance, level_num_bitplanes);
            // print ratios
            print_ratio(retrieve_sizes);
            // retrieve data
            auto concated_level_components = retriever.retrieve_level_components(offsets, retrieve_sizes);
            interleave_level_components(concated_level_components);
            // increment offset for progressive reading
            for(int i=0; i<offsets.size(); i++){
                offsets[i] += retrieve_sizes[i];
            }
            // check whether to reconstruct to full resolution
            uint8_t target_level = level_error_bounds.size() - 1;
            int skipped_level = 0;
            for(int i=0; i<=target_level; i++){
                if(level_num_bitplanes[target_level - i] != 0){
                    skipped_level = i;
                    break;
                }
            }
            target_level -= skipped_level;

            bool success = reconstruct(target_level);
            for(int i=0; i<concated_level_components.size(); i++){
                free(concated_level_components[i]);
            }
            if(success) return data.data();
            else{
                std::cerr << "Recontruct unsuccessful, return NULL pointer" << std::endl;
                return NULL;
            }
        }

        void load_metadata(){
            uint8_t * metadata = retriever.load_metadata();
            uint8_t const * metadata_pos = metadata;
            uint8_t num_dims = *(metadata_pos ++);
            deserialize(metadata_pos, num_dims, dimensions);
            uint8_t num_levels = *(metadata_pos ++);
            deserialize(metadata_pos, num_levels, level_error_bounds);
            deserialize(metadata_pos, num_levels, level_errors);
            deserialize(metadata_pos, num_levels, level_sizes);
            offsets = std::vector<uint32_t>(num_levels, 0);
            free(metadata);
        }

        ~ComposedReconstructor(){}

        void print() const {
            std::cout << "Composed reconstructor with the following components." << std::endl;
            std::cout << "Decomposer: "; decomposer.print();
            std::cout << "Interleaver: "; interleaver.print();
            std::cout << "Encoder: "; encoder.print();
            std::cout << "Retriever: "; retriever.print();
        }
    private:

        void print_ratio(const std::vector<uint32_t>& retrieve_sizes){
            uint32_t total_size = 0;
            for(const auto& size:retrieve_sizes){
                total_size += size;
            }
            uint32_t num_elements = 1;
            for(const auto& d:dimensions){
                num_elements *= d;
            }
            for(int i=0; i<level_num_bitplanes.size(); i++){
                std::cout << "Retrieve " << +level_num_bitplanes[i] << " bitplanes from level " << i << std::endl;
            }
            std::cout << "Total retrieved size = " << total_size << ", ratio = " << num_elements * 1.0 * sizeof(T) / total_size << std::endl; 
        }

        bool interleave_level_components(const std::vector<uint8_t*>& concated_level_components){
            level_components.clear();
            for(int i=0; i<level_num_bitplanes.size(); i++){
                const uint8_t * pos = concated_level_components[i];
                std::vector<const uint8_t*> interleaved_level;
                for(int j=0; j<level_num_bitplanes[i]; j++){
                    interleaved_level.push_back(pos);
                    pos += level_sizes[i][j];
                }
                level_components.push_back(interleaved_level);
            }
            return true;
        }

        bool reconstruct(uint8_t target_level){
            auto level_dims = compute_level_dims(dimensions, target_level);
            auto reconstruct_dimensions = level_dims[target_level];
            uint32_t num_elements = 1;
            for(const auto& dim:reconstruct_dimensions){
                num_elements *= dim;
            }
            data.clear();
            data = std::vector<T>(num_elements, 0);

            auto lossless_compressor = ZSTD();
            auto level_elements = compute_level_elements(level_dims, target_level);
            std::vector<uint32_t> dims_dummy(reconstruct_dimensions.size(), 0);
            for(int i=0; i<=target_level; i++){
                std::vector<const uint8_t*> decompressed_level_compenents;
                std::vector<uint8_t*> lossless_decompressed;
                for(int j=0; j<level_num_bitplanes[i]; j++){
                    uint8_t * decompressed = NULL;
                    auto decompressed_size = lossless_compressor.decompress(level_components[i][j], level_sizes[i][j], &decompressed);
                    decompressed_level_compenents.push_back(decompressed);
                    if(decompressed != level_components[i][j]){
                        lossless_decompressed.push_back(decompressed);                    
                    }
                }
                int level_exp = 0;
                frexp(level_error_bounds[i], &level_exp);
                auto level_decoded_data = encoder.decode(decompressed_level_compenents, level_elements[i], level_exp, level_num_bitplanes[i]);
                for(int j=0; j<lossless_decompressed.size(); j++){
                    free(lossless_decompressed[j]);
                }

                const std::vector<uint32_t>& prev_dims = (i == 0) ? dims_dummy : level_dims[i - 1];
                interleaver.reposition(level_decoded_data, reconstruct_dimensions, level_dims[i], prev_dims, data.data());
                free(level_decoded_data);
            }
            decomposer.recompose(data.data(), reconstruct_dimensions, target_level);
            return true;
        }

        Decomposer decomposer;
        Interleaver interleaver;
        Encoder encoder;
        SizeInterpreter interpreter;
        Retriever retriever;
        std::vector<uint32_t> offsets;
        std::vector<T> data;
        std::vector<uint32_t> dimensions;
        std::vector<T> level_error_bounds;
        std::vector<uint8_t> level_num_bitplanes;
        std::vector<std::vector<const uint8_t*>> level_components;
        std::vector<std::vector<uint32_t>> level_sizes;
        std::vector<std::vector<double>> level_errors;
    };
}
#endif
