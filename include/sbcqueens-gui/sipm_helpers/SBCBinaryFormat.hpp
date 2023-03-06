//
// Created by Hector Hawley Herrera on 2023-02-19.
//

#ifndef SBCBINARYFORMAT_H
#define SBCBINARYFORMAT_H
#include <algorithm>
#include <cstdint>
#include <tuple>
#pragma once

// C STD includes
// C 3rd party includes
// C++ STD includes
#include <bit>
#include <cinttypes>
#include <numeric>
#include <fstream>
#include <type_traits>
#include <filesystem>
#include <algorithm>
#include <array>

// C++ 3rd party includes
#include <concurrentqueue.h>
#include <spdlog/spdlog.h>

// my includes
#include "sbcqueens-gui/file_helpers.hpp"
#include "sbcqueens-gui/caen_helper.hpp"

namespace SBCQueens::BinaryFormat {
namespace Tools {

    // The BinaryFormat only accepts arithmethic pointers.
    // ex: const char* returns true
    //      std::string return false
    //      double* return true;
    template<typename T>
    concept is_arithmethic_ptr = std::is_arithmetic_v<
                                        std::remove_cvref_t<
                                        std::remove_pointer_t<T>>> and not std::is_pointer_v<T>;


    template<typename... T>
    concept is_arithmetic_ptr_unpack = requires(T x) {
            (... and is_arithmethic_ptr<T>); };

    template<typename T>
    requires is_arithmethic_ptr<T>
    constexpr static std::string_view type_to_string() {
        // We get the most pure essence of T: no consts, no references,
        // and no []
        using T_no_const = std::remove_pointer_t<std::remove_all_extents_t<
                std::remove_reference_t<std::remove_const_t<T>>>>;
        if constexpr (std::is_same_v<T_no_const, char>) {
            return "char";
        } else if constexpr (std::is_same_v<T_no_const, uint8_t>) {
            return "uint8";
        } else if constexpr (std::is_same_v<T_no_const, uint16_t>) {
            return "uint16";
        } else if constexpr (std::is_same_v<T_no_const, uint32_t>) {
            return "uint32";
        } else if constexpr (std::is_same_v<T_no_const, uint64_t>) {
            return "uint64";
        } else if constexpr (std::is_same_v<T_no_const, int8_t>) {
            return "int8";
        } else if constexpr (std::is_same_v<T_no_const, int16_t>) {
            return "int16";
        } else if constexpr (std::is_same_v<T_no_const, int32_t>) {
            return "int32";
        } else if constexpr (std::is_same_v<T_no_const, int64_t>) {
            return "int64";
        } else if constexpr (std::is_same_v<T_no_const, float>) {
            return "single";
        } else if constexpr (std::is_same_v<T_no_const, double>) {
            return "double";
        } else if constexpr (std::is_same_v<T_no_const, long double>) {
            return "float128";
        }
        // TODO(All): maybe the default should be uint32? or no default?
    }

} // namespace Tools

/*  SBC Binary Header description:
 * Header of a binary format is divided in 4 parts:
 * 1.- Edianess            - always 4 bits long (uint32_t)
 * 2.- Data Header size    - always 2 bits long (uint16_t)
 * and is the length of the next bit of data
 * 3.- Data Header         - is data header long.
 * Contains the structure of each line. It is always found as a raw
 * string in the form "{name_col};{type_col};{size1},{size2}...;...;
 * Cannot be longer than 65536 bytes.
 * 4.- Number of lines     - always 4 bits long (int32_t)
 * Number of lines in the file. If 0, it is indefinitely long.
*/
template<typename... DataTypes>
requires Tools::is_arithmetic_ptr_unpack<DataTypes...>
struct DynamicWriter {
    //TODO(Any): make it possible to take both normal arithmetic types
    //  - and their corresponding array types Ex: int and int[]
    //  - that is to assume that if int is passed, it mean we want a scalar
    //  - and int[] would mean an array.
    using tuple_type = std::tuple<std::span<DataTypes>...>;
    constexpr static std::size_t n_cols = sizeof...(DataTypes);
    constexpr static std::array<std::size_t, n_cols> size_of_types = { sizeof(DataTypes)... };
    constexpr static std::array<std::string_view, n_cols> parameters_types_str = { Tools::type_to_string<DataTypes>()... };

 private:
    const std::string _file_name;
    const std::array<std::string, n_cols> _names;
    const std::array<std::size_t, n_cols> _ranks;
    const std::vector<std::size_t> _sizes;

    std::size_t total_ranks = 0;
    bool _open = false;
    std::fstream _stream;

    std::size_t _line_byte_size = 0;
    std::size_t _line_param_order = 0;
    std::size_t _line_buffer_loc = 0;
    std::string _line_buffer;

    template<typename T>
    void _copy_number_to_buff(const T& num,
                              std::string& buffer,
                              std::size_t& loc,
                              const std::size_t& size = sizeof(T)) {
        const char* tmpstr = reinterpret_cast<const char*>(&num);
        for(std::size_t i = 0; i < size; i++) {
            buffer[i + loc] = tmpstr[i];
        }

        loc += size;
    }

    void _copy_str_to_buff(std::string_view source,
                           std::string& buffer,
                           std::size_t& loc) {
        source.copy(&buffer[loc], source.length(), 0);
        loc += source.length();
    }

    std::string _build_header() {
        // Edianess first
        uint32_t endianess = 0x01020304;
        if constexpr (std::endian::native == std::endian::big) {
            endianess = 0x04030201;
        } else {
            endianess = 0x01020304;
        }

        // calculates
        std::size_t total_header_size = 0;
        // has to be uint16_t because we are saving it to the file later

        uint16_t binary_header_size = 0;
        std::size_t total_ranks_so_far = 0;
        for (std::size_t i = 0; i < n_cols; i++) {
            auto column_rank = _ranks[i];
            // + 1 for the ; character
            binary_header_size += _names[i].length() + 1;
            // + 1 for the ; character
            binary_header_size += parameters_types_str[i].length() + 1;

            std::size_t total_rank_size = 1;
            for(std::size_t j = 0; j < column_rank; j ++) {
                auto size = _sizes[total_ranks_so_far + j];
                total_rank_size *= size;
                // It is always + 1 because there is either a ',' or a ';'
                binary_header_size += std::to_string(size).length() + 1;
            }

            total_ranks_so_far += column_rank;
            _line_byte_size += size_of_types[i]*total_rank_size;
        }

        // We also calculate the line size very useful when we start data saving
        total_header_size += sizeof(uint32_t); // 1.
        total_header_size += sizeof(uint16_t); // 2.
        total_header_size += binary_header_size; // 3.
        total_header_size += sizeof(int32_t); // 4.

        // Now we allocate the memory!
        std::size_t buffer_loc = 0;
        auto buffer = std::string(total_header_size, 'A');
        _line_buffer = std::string(_line_byte_size, 'A');

        // Now we fill buffer.
        total_ranks_so_far = 0;
        _copy_number_to_buff(endianess, buffer, buffer_loc); // 1.
        _copy_number_to_buff(binary_header_size, buffer, buffer_loc); // 2.
        for (std::size_t i = 0; i < n_cols; i++) { // 3.
            auto column_name = _names[i];
            auto column_type = parameters_types_str[i];
            auto column_rank = _ranks[i];

            _copy_str_to_buff(column_name, buffer, buffer_loc);
            _copy_str_to_buff(";", buffer, buffer_loc);
            _copy_str_to_buff(column_type, buffer, buffer_loc);
            _copy_str_to_buff(";", buffer, buffer_loc);
            for(std::size_t size_j = 0; size_j < column_rank; size_j++) {
                _copy_str_to_buff(std::to_string(_sizes[total_ranks_so_far + size_j]),
                                  buffer, buffer_loc);

                if(size_j != column_rank - 1) {
                    _copy_str_to_buff(",", buffer, buffer_loc);
                }
            }

            _copy_str_to_buff(";", buffer, buffer_loc);
            total_ranks_so_far += column_rank;
        }

        // For dynamic files, this is always 0!
        const int32_t num_lines = 0x00000000;
        _copy_number_to_buff(num_lines, buffer, buffer_loc); // 4.
        // Done!
        return buffer;
    }

    // Save item from tuple in position i
    template<std::size_t i>
    void _save_item(const tuple_type& items, std::size_t& loc) {
        auto item = std::get<i>(items);

        auto rank = _ranks[i];
        auto total_rank_up_to_i = std::accumulate(_ranks.begin(),
                                                  &_ranks[i],
                                                  0);

        const auto& size_index_start = total_rank_up_to_i;
        const std::size_t expected_size = std::accumulate(&_sizes[size_index_start],
                                                 &_sizes[size_index_start + rank],
                                                 1,
                                                std::multiplies<std::size_t>());

        if (expected_size != item.size()) {
            throw std::out_of_range("memory is out of range");
        }

//        std::memcpy(&_line_buffer[loc], item.data(), item.size_bytes());
        auto item_bytes =  std::as_bytes(item);
        for(std::size_t byte = 0; byte < item.size_bytes(); byte++) {
            _line_buffer.at(loc + byte) = static_cast<char>(item_bytes[byte]);
        }
        loc += item.size_bytes();
    }

    // Think of t his function as a wrapper between _save_item
    // and _save_data
    template<std::size_t... I>
    void _save_item_helper(const tuple_type& data, std::index_sequence<I...>) {
        (_save_item<I>(data, _line_buffer_loc),...);
    }

    void _save_event(const tuple_type& data) {
        _line_buffer_loc = 0;
        _save_item_helper(data, std::make_index_sequence<n_cols>{});
        _stream << _line_buffer;
    }

 public:
    DynamicWriter(std::string_view file_name,
                  const std::array<std::string, n_cols>& columns_names,
                  const std::array<std::size_t, n_cols>& columns_ranks,
                  const std::vector<std::size_t>& columns_sizes) :
        _file_name{file_name},
        _names{columns_names},
        _ranks{columns_ranks},
        _sizes{columns_sizes}
    {
        total_ranks = std::accumulate(columns_ranks.begin(),
                                      columns_ranks.end(), 0);

        if (std::filesystem::exists(file_name)) {
            if (std::filesystem::is_empty(file_name)) {
                // If file is empty or does not exist, then we
                // open it and write to it.
                _stream.open(_file_name, std::ios::app | std::ofstream::binary);
                if (_stream.is_open()) {
                    _open = true;
                    _stream << _build_header();
                }
            } else {
                std::ifstream peeker(_file_name, std::ofstream::binary);

                std::string header = _build_header();
                std::string current_file_header(header.length(), '\0');
                peeker.seekg(0);
                peeker.read(&current_file_header[0], header.length());

                if (current_file_header != header) {
                    throw std::runtime_error("File being written to has an "
                                             "incompatible header format. "
                                             "Details:\n\t File = " + _file_name);
                }

                // We do not write anything.
                _stream.open(_file_name, std::ios::app | std::ofstream::binary);
                _open = _stream.is_open();

            }
        } else {
            _stream.open(_file_name, std::ios::app | std::ofstream::binary);
            if (_stream.is_open()) {
                _open = true;
                _stream << _build_header();
            }
        }
    }

    bool isOpen() { return _open; }

    ~DynamicWriter() {
        _open = false;
        _stream.flush();
        _stream.close();
    }

    void save(std::span<DataTypes>... data) {
        if(_open) {
            _save_event(std::make_tuple(data...));
        }
    }
};

// TODO(Hector): we need this!
template<typename... DataTypes>
struct Reader {



 private:
};

class SiPMDynamicWriter {
    using SiPMDW = DynamicWriter<   double,    // sample rate
                                    uint8_t,   // Enabled Channels
                                    uint64_t,  // Trigger Mask
                                    uint16_t,  // Thresholds
                                    uint16_t,  // DC Offsets
                                    uint8_t,   // DC Corrections
                                    float,     // DC Range
                                    uint32_t,  // Time stamp
                                    uint32_t,  // Trigger source
                                    uint16_t>; // Waveforms

    constexpr static std::size_t num_cols = 10;
    constexpr static std::array<std::size_t, num_cols> sipm_ranks =
                                    {1, 1, 1, 1, 1, 1, 1, 1, 1, 2};
    const inline static std::array<std::string, num_cols> column_names =
            {"sample_rate", "en_chs", "trg_mask", "thresholds", "dc_offsets",
             "dc_corrections", "dc_range", "time_stamp", "trg_source", "sipm_traces"};


    double _sample_rate[1] = {0.0};
    std::vector<std::uint8_t> _en_chs;
    uint64_t _trigger_mask[1] = {0};
    std::vector<uint16_t> _thresholds;
    std::vector<uint16_t> _dc_offsets;
    std::vector<uint8_t> _dc_corrections;
    std::vector<float> _dc_ranges;

    uint32_t _trigger_tag[1] = {0};
    uint32_t _trigger_source[1] = {0};

    uint32_t _record_length;
    SiPMDW _streamer;
 public:
    /* Details of each parameters:
    Name          | type      | length (in Bytes) | is a constant?|
    ---------------------------------------------------------------
    sample_rate   | double    | 8                 | Y
    en_chs        | uint8     | 1*ch_size         | Y
    trg_mask      | uint64    | 8                 | Y
    thresholds    | uint16    | 2*ch_size         | Y
    dc_offsets    | uint16    | 2*ch_size         | Y
    dc_corrections| uint8     | 1*ch_size         | Y
    dc_range      | single    | 4*ch_size         | Y
    time_stamp    | uint32    | 4                 | N
    trg_source    | uint32    | 4                 | N
    data          | uint16    | 2*rl*ch_size      | N
    ---------------------------------------------------------------
    rl -> record length of the waveforms
    ch_size -> number of enabled channels
    en_chs  -> the channels # that were enabled

    Total length = 24 + ch_size*(10 + 2*record_length)
    */

    SiPMDynamicWriter(std::string_view file_name,
                      const CAENDigitizerModelConstants& model_consts,
                      const CAENGlobalConfig& global_config,
                      const std::array<CAENGroupConfig, 8>& group_configs) :
        _sample_rate{model_consts.AcquisitionRate},
        _en_chs{_get_en_chs(model_consts, group_configs)},
        _record_length{global_config.RecordLength},
        _streamer{file_name, column_names,  sipm_ranks, _form_sizes(global_config)}
    {
        for(auto ch : _en_chs) {
            CAENGroupConfig group;
            if (model_consts.NumberOfGroups == 0) {
                group = group_configs[ch];
                _dc_corrections.push_back(group.DCCorrections[0]);
            } else {
                group =  group_configs[ch % 8];
                _dc_corrections.push_back(group.DCCorrections[ch % 8]);
            }

            _trigger_mask[0] |= (1 << ch);  // flip the bit at position ch
            _thresholds.push_back(group.TriggerThreshold);
            _dc_offsets.push_back(group.DCOffset);
            _dc_ranges.push_back(static_cast<float>(
                    model_consts.VoltageRanges.at(group.DCRange)));
        }
    }

    ~SiPMDynamicWriter() = default;

    bool isOpen() { return _streamer.isOpen(); }

    void save_waveform(const std::shared_ptr<CAENWaveforms<uint16_t>>& waveform) {
        _trigger_tag[0] = waveform->getInfo().TriggerTimeTag;
        _trigger_source[0] = waveform->getInfo().Pattern;
        _streamer.save(_sample_rate,
                      _en_chs,
                      _trigger_mask,
                      _thresholds,
                      _dc_offsets,
                      _dc_corrections,
                      _dc_ranges,
                      _trigger_tag,
                      _trigger_source,
                      waveform->getData());
    }

 private:

    std::vector<std::size_t> _form_sizes(
        const CAENGlobalConfig& caen_global_config) {

        auto num_en_chs = _en_chs.size();
        return {1, num_en_chs, 1, num_en_chs, num_en_chs, num_en_chs, num_en_chs,
                1, 1, num_en_chs, caen_global_config.RecordLength};
    }

    std::vector<std::uint8_t> _get_en_chs(
            const CAENDigitizerModelConstants& model_constants,
            const std::array<CAENGroupConfig, 8>& groups) {
        std::vector<std::uint8_t> out;
        for(std::size_t group_num = 0; group_num < groups.size(); group_num++) {
            const auto& group = groups[group_num];
            if (not group.Enabled) {
                continue;
            }

            // If the digitizer does not support groups, group_num = ch
            if(model_constants.NumberOfGroups == 0) {
                out.push_back(group_num);
                continue;
            }

            // Othewise, calculate using the AcquisitionMask
            for(std::size_t ch = 0; ch < model_constants.NumChannelsPerGroup; ch++) {
                if(group.AcquisitionMask.at(ch)) {
                    out.push_back(ch + model_constants.NumChannelsPerGroup * group_num);
                }
            }
        }
        return out;
    }


};

} // namespace SBCQueens::BinaryFormat

#endif //SBCBINARYFORMAT_H
