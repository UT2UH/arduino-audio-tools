#pragma once

#include "AudioTools.h"

#define WAV_FORMAT_PCM 0x0001
#define TAG(a, b, c, d) (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))

namespace audio_tools {

/**
 * @brief Sound information which is available in the WAV header
 * @author Phil Schatzmann
 * @copyright GPLv3
 * 
 */
struct WAVAudioInfo {
    int format;
    int sample_rate;
    int bits_per_sample;
    int channels;
    int byte_rate;
    int block_align;
    bool is_streamed;
    bool is_valid;
    uint32_t data_length;
    uint32_t file_size;
};


/**
 * @brief Parser for Wav header data
 * for details see https://de.wikipedia.org/wiki/RIFF_WAVE
 * @author Phil Schatzmann
 * @copyright GPLv3
 * 
 */
class WAVHeader  {
    public:
        WAVHeader(){
        };

        void begin(uint8_t* buffer, size_t len){
            LOGI("WAVHeader len: %u", len);

            this->buffer = buffer;
            this->len = len;
            this->data_pos = 0l;
            
            memset(&headerInfo, 0, sizeof(WAVAudioInfo));
            while (!eof()) {
                uint32_t tag, tag2, length;
                tag = read_tag();
                if (eof())
                    break;
                length = read_int32();
                if (!length || length >= 0x7fff0000) {
                    headerInfo.is_streamed = true;
                    length = ~0;
                }
                if (tag != TAG('R', 'I', 'F', 'F') || length < 4) {
                    seek(length, SEEK_CUR);
                    continue;
                }
                tag2 = read_tag();
                length -= 4;
                if (tag2 != TAG('W', 'A', 'V', 'E')) {
                    seek(length, SEEK_CUR);
                    continue;
                }
                // RIFF chunk found, iterate through it
                while (length >= 8) {
                    uint32_t subtag, sublength;
                    subtag = read_tag();
                    if (eof())
                        break;
                    sublength = read_int32();
                    length -= 8;
                    if (length < sublength)
                        break;
                    if (subtag == TAG('f', 'm', 't', ' ')) {
                        if (sublength < 16) {
                            // Insufficient data for 'fmt '
                            break;
                        }
                        headerInfo.format          = read_int16();
                        headerInfo.channels        = read_int16();
                        headerInfo.sample_rate     = read_int32();
                        headerInfo.byte_rate       = read_int32();
                        headerInfo.block_align     = read_int16();
                        headerInfo.bits_per_sample = read_int16();
                        if (headerInfo.format == 0xfffe) {
                            if (sublength < 28) {
                                // Insufficient data for waveformatex
                                break;
                            }
                            skip(8);
                            headerInfo.format = read_int32();
                            skip(sublength - 28);
                        } else {
                            skip(sublength - 16);
                        }
                        headerInfo.is_valid = true;
                    } else if (subtag == TAG('d', 'a', 't', 'a')) {
                        sound_pos = tell();
                        headerInfo.data_length = sublength;
                        if (!headerInfo.data_length || headerInfo.is_streamed) {
                            headerInfo.is_streamed = true;
                            logInfo();
                            return;
                        }
                        seek(sublength, SEEK_CUR);
                    } else {
                        skip(sublength);
                    }
                    length -= sublength;
                }
                if (length > 0) {
                    // Bad chunk?
                    seek(length, SEEK_CUR);
                }
            }
            logInfo();
            return;
        }


        // provides the AudioInfo
        WAVAudioInfo &audioInfo() {
            return headerInfo;
        }

        // provides access to the sound data for the first record
        bool soundData(uint8_t* &data, size_t &len){
            if (sound_pos > 0){
                data = buffer + sound_pos;
                len = max((long) (this->len - sound_pos),0l);
                sound_pos = 0;
                return true;
            }
            return false;
        }

    protected:
        struct WAVAudioInfo headerInfo;
        uint8_t* buffer;
        size_t len;
        size_t data_pos = 0;
        size_t sound_pos = 0;

        uint32_t read_tag() {
            uint32_t tag = 0;
            tag = (tag << 8) | getChar();
            tag = (tag << 8) | getChar();
            tag = (tag << 8) | getChar();
            tag = (tag << 8) | getChar();
            return tag;
        }

        uint32_t read_int32() {
            uint32_t value = 0;
            value |= getChar() <<  0;
            value |= getChar() <<  8;
            value |= getChar() << 16;
            value |= getChar() << 24;
            return value;
        }

        uint16_t read_int16() {
            uint16_t value = 0;
            value |= getChar() << 0;
            value |= getChar() << 8;
            return value;
        }

        void skip(int n) {
            int i;
            for (i = 0; i < n; i++)
                getChar();
        }

        int getChar() {
            if (data_pos<len) 
                return buffer[data_pos++];
            else
                return -1;
        }

        void seek(long int offset, int origin ){
            if (origin==SEEK_SET){
                data_pos = offset;
            } else if (origin==SEEK_CUR){
                data_pos+=offset;
            }
        }

        size_t tell() {
            return data_pos;
        }

        bool eof() {
            return data_pos>=len-1;
        }

        void logInfo(){
            LOGI("WAVHeader sound_pos: %d", sound_pos);
            LOGI("WAVHeader channels: %d ", headerInfo.channels);
            LOGI("WAVHeader bits_per_sample: %d", headerInfo.bits_per_sample);
            LOGI("WAVHeader sample_rate: %d ", headerInfo.sample_rate);
            LOGI("WAVHeader format: %d", headerInfo.format);
        }

};


/**
 * @brief WAVDecoder - We parse the header data on the first record
 * and send the sound data to the stream which was indicated in the
 * constructor. Only WAV files with WAV_FORMAT_PCM are supported!
 * 
 * @author Phil Schatzmann
 * @copyright GPLv3
 */
class WAVDecoder : public AudioWriter {
    public:
        /**
         * @brief Construct a new WAVDecoder object
         * 
         * @param out_stream Output Stream to which we write the decoded result
         */
        WAVDecoder(Print &out_stream){
            this->out = &out_stream;
            this->audioBaseInfoSupport = nullptr;
        }

        /**
         * @brief Construct a new WAVDecoder object
         * 
         * @param out_stream Output Stream to which we write the decoded result
         * @param bi Object that will be notified about the Audio Formt (Changes)
         */

        WAVDecoder(Print &out_stream, AudioBaseInfoDependent &bi){
            this->out = &out_stream;
            this->audioBaseInfoSupport = &bi;
        }
    
        void begin() {
            isFirst = true;
            active = true;
        }

        WAVAudioInfo &audioInfo() {
            return header.audioInfo();
        }

        virtual size_t write(const void *in_ptr, size_t in_size) {
            size_t result = 0;
            if (active) {
                if (isFirst){
                    header.begin((uint8_t*)in_ptr, in_size);
                    uint8_t *sound_ptr;
                    size_t len;
                    if (header.soundData(sound_ptr, len)){
                        isFirst = false;
                        isValid = header.audioInfo().is_valid;

                        LOGI("WAV sample_rate: %d", header.audioInfo().sample_rate);
                        LOGI("WAV data_length: %d", header.audioInfo().data_length);
                        LOGI("WAV is_streamed: %d", header.audioInfo().is_streamed);
                        LOGI("WAVis_valid: %d", header.audioInfo().is_valid);
                        
                        // check format
                        int format = header.audioInfo().format;
                        isValid = format == WAV_FORMAT_PCM;
                        if (format != WAV_FORMAT_PCM){
                            LOGE("WAV format not supported: %d", format);
                            isValid = false;
                        } else {
                            // update sampling rate if the target supports it
                            AudioBaseInfo bi;
                            bi.sample_rate = header.audioInfo().sample_rate;
                            bi.channels = header.audioInfo().channels;
                            bi.bits_per_sample = header.audioInfo().bits_per_sample;
                            // we provide some functionality so that we could check if the destination supports the requested format
                            if (audioBaseInfoSupport!=nullptr){
                                isValid = audioBaseInfoSupport->validate(bi);
                                if (isValid){
                                    LOGI("isValid: %s", isValid ? "true":"false");
                                    audioBaseInfoSupport->setAudioInfo(bi);
                                    // write prm data from first record
                                    LOGI("WAVDecoder writing first sound data");
                                    result = out->write(sound_ptr, len);
                                } else {
                                    LOGE("isValid: %s", isValid ? "true":"false");
                                }
                            }
                        }
                    }
                } else if (isValid)  {
                    result = out->write((uint8_t*)in_ptr, in_size);
                }
            }
            return result;
        }

        virtual operator boolean() {
            return active;
        }

    protected:
        WAVHeader header;
        Print *out;
        AudioBaseInfoDependent *audioBaseInfoSupport;
        bool isFirst = true;
        bool isValid = true;
        bool active;

};



/**
 * @brief A simple WAV file encoder. 
 * @author Phil Schatzmann
 * @copyright GPLv3
 */
class WAVEncoder : public AudioWriter {
    public: 
        // Empty Constructor - the output stream must be provided with begin()
        WAVEncoder(){
        }        

        // Constructor providing the output stream
        WAVEncoder(Stream &out){
            stream_ptr = &out;
        }

        // Provides the default configuration
        WAVAudioInfo defaultConfig() {
            WAVAudioInfo info;
            info.format = WAV_FORMAT_PCM;
            info.sample_rate = DEFAULT_SAMPLE_RATE;
            info.bits_per_sample = DEFAULT_BITS_PER_SAMPLE;
            info.channels = DEFAULT_CHANNELS;
            info.is_streamed = false;
            info.is_valid = true;
            info.data_length = 0x7fff0000;
            info.file_size = info.data_length + 36;
            return info;
        }

        // starts the processing
        virtual void begin(WAVAudioInfo &ai) {
            header_written = false;
            is_open = true;
            audioInfo = ai;
            audioInfo.byte_rate = audioInfo.sample_rate * audioInfo.bits_per_sample * audioInfo.channels;
            audioInfo.block_align =  audioInfo.bits_per_sample / 8 * audioInfo.channels;
            if (audioInfo.is_streamed || audioInfo.data_length==0 || audioInfo.data_length >= 0x7fff0000) {
                LOGI("is_streamed! because length is %u", audioInfo.data_length);
                audioInfo.is_streamed = true;
                audioInfo.data_length = ~0;
            } else {
                size_limit = audioInfo.data_length;
                LOGI("size_limit is %ld", size_limit);
            }
        }

        // starts the processing
        virtual void begin(Stream &out, WAVAudioInfo &ai) {
            stream_ptr = &out;
            begin(ai);
        }

        void end() {
            is_open = false;
        }

        // Writes PCM data to be encoded as WAV
        virtual size_t write(const void *in_ptr, size_t in_size){
            if (!is_open){
                LOGE("The WAVEncoder is not open - please call begin()");
                int x = 10 / 0;
                return 0;
            }
            if (stream_ptr==nullptr){
                LOGE("No output stream was provided");
                return 0;
            }
            if (!header_written){
                LOGI("Writing Header");
                writeRiffHeader();
                writeFMT();
                writeDataHeader();
                header_written = true;
            }

            int32_t result = 0;
            if (audioInfo.is_streamed){
                result = stream_ptr->write((uint8_t*)in_ptr, in_size);
            } else if (size_limit>0){
                size_t write_size = min((size_t)in_size,(size_t)size_limit);
                result = stream_ptr->write((uint8_t*)in_ptr, write_size);
                size_limit -= result;

                if (size_limit<=0){
                    LOGI("The defined size was written - so we close the WAVEncoder now");
                    stream_ptr->flush();
                    is_open = false;
                }
            }  
            return result;
        }

        operator boolean() {
            return is_open;
        }

        bool isOpen(){
            return is_open;
        }

    protected:
        Stream* stream_ptr;
        WAVAudioInfo audioInfo = defaultConfig();
        int64_t size_limit;
        bool header_written = false;
        volatile bool is_open;

        void writeRiffHeader(){
            stream_ptr->write("RIFF",4);
            write32(*stream_ptr, audioInfo.file_size-8);
            stream_ptr->write("WAVE",4);
        }

        void writeFMT(){
            uint16_t fmt_len = 16;
            uint32_t byteRate = audioInfo.sample_rate * audioInfo.bits_per_sample * audioInfo.channels / 8;
            uint32_t frame_size = audioInfo.channels * audioInfo.bits_per_sample / 8;
            stream_ptr->write("fmt ",4);
            write32(*stream_ptr, fmt_len);
            write16(*stream_ptr, audioInfo.format); //PCM
            write16(*stream_ptr, audioInfo.channels); 
            write32(*stream_ptr, audioInfo.sample_rate); 
            write32(*stream_ptr, byteRate); 
            write16(*stream_ptr, frame_size);  //frame size
            write16(*stream_ptr, audioInfo.bits_per_sample);             
        }

        void write32(Stream &stream, uint64_t value ){
            stream.write((uint8_t *) &value, 4);
        }
        void write16(Stream &stream, uint16_t value ){
            stream.write((uint8_t *) &value, 2);
        }

        void writeDataHeader() {
            stream_ptr->write("data",4);
            audioInfo.file_size -=44;
            write32(*stream_ptr, audioInfo.file_size);
        }

};

}
