#pragma once
#include "AudioTypes.h"
#include "Vector.h"

namespace audio_tools {


static int32_t convertFrom24To32(int24_t value)  {
    return value.scale32();
}

static int16_t convertFrom24To16(int24_t value)  {
    return value.scale16();
}

static float convertFrom24ToFloat(int24_t value)  {
    return value.scaleFloat();
}

static int16_t convertFrom32To16(int32_t value)  {
    return static_cast<float>(value) / INT32_MAX * INT16_MAX;
}


/**
 * @brief Abstract Base class for Converters
 * A converter is processing the data in the indicated array
 * @author Phil Schatzmann
 * @copyright GPLv3
 * @tparam T 
 */
template<typename T>
class BaseConverter {
    public:
        virtual void convert(T (*src)[2], size_t size) = 0;
};


/**
 * @brief Dummy converter which does nothing
 * 
 * @tparam T 
 */
template<typename T>
class NOPConverter : public  BaseConverter<T> {
    public:
        virtual void convert(T (*src)[2], size_t size) {};
};

/**
 * @brief Multiplies the values with the indicated factor adds the offset and clips at maxValue. To mute use a factor of 0.0!
 * @author Phil Schatzmann
 * @copyright GPLv3
 * 
 * @tparam T 
 */
template<typename T>
class ConverterScaler : public  BaseConverter<T> {
    public:
        ConverterScaler(float factor, T offset, T maxValue){
            this->factor = factor;
            this->maxValue = maxValue;
            this->offset = offset;
        }

        void convert(T (*src)[2], size_t size) {
            for (size_t j=0;j<size;j++){
                src[j][0] = (src[j][0] + offset) * factor;
                if (src[j][0]>maxValue){
                    src[j][0] = maxValue;
                } else if (src[j][0]<-maxValue){
                    src[j][0] = -maxValue;
                }
                src[j][1] = src[j][1] + offset * factor;
                if (src[j][1]>maxValue){
                    src[j][1] = maxValue;
                } else if (src[j][0]<-maxValue){
                    src[j][1] = -maxValue;
                }
            }
        }
    protected:
        float factor;
        T maxValue;
        T offset;
};

/**
 * @brief Makes sure that the avg of the signal is set to 0
 * 
 * @tparam T 
 */
template<typename T>
class ConverterAutoCenter : public  BaseConverter<T> {
    public:
        ConverterAutoCenter(){
        }

        void convert(T (*src)[2], size_t size) {
            setup(src, size);
            if (is_setup){
                for (size_t j=0; j<size; j++){
                    src[j][0] = src[j][0] - offset;
                    src[j][1] = src[j][1] - offset;
                }
            }
        }

    protected:
        T offset;
        float left;
        float right;
        bool is_setup = false;

        void setup(T (*src)[2], size_t size){
            if (!is_setup) {
                for (size_t j=0;j<size;j++){
                    left += src[j][0];
                    right += src[j][1];
                }
                left = left / size;
                right = right / size;

                if (left>0){
                    offset = left;
                    is_setup = true;
                } else if (right>0){
                    offset = right;
                    is_setup = true;
                }
            }
        }
};

/**
 * @brief Switches the left and right channel
 * @author Phil Schatzmann
 * @copyright GPLv3
 * 
 * @tparam T 
 */
template<typename T>
class ConverterSwitchLeftAndRight : public  BaseConverter<T> {
    public:
        ConverterSwitchLeftAndRight(){
        }
        void convert(T (*src)[2], size_t size) {
            for (size_t j=0;j<size;j++){
                src[j][1] = src[j][0];
                src[j][0] = src[j][1];
            }
        }
};

/**
 * @brief Make sure that both channels contain any data
 * @author Phil Schatzmann
 * @copyright GPLv3
 * 
 * @tparam T 
 */
template<typename T>
class ConverterFillLeftAndRight : public  BaseConverter<T> {
    public:
        ConverterFillLeftAndRight(){
        }
        void convert(T (*src)[2], size_t size) {
            setup(src, size);
            if (left_empty && !right_empty){
                for (size_t j=0;j<size;j++){
                    src[j][0] = src[j][1];
                }
            } else if (!left_empty && right_empty) {
                for (size_t j=0;j<size;j++){
                    src[j][1] = src[j][0];
                }
            }
        }

    private:
        bool is_setup = false;
        bool left_empty = true;
        bool right_empty = true; 

        void setup(T src[][2], size_t size) {
            if (!is_setup) {
                for (int j=0;j<size;j++) {
                    if (src[j][0]!=0) {
                        left_empty = false;
                        break;
                    }
                }
                for (int j=0;j<size;j++) {
                    if (src[j][1]!=0) {
                        right_empty = false;
                        break;
                    }
                }
                // stop setup as soon as we found some data
                if (!right_empty || !left_empty) {
                    is_setup = true;
                }
            }
        }

};

/**
 * @brief special case for internal DAC output, the incomming PCM buffer needs 
 *  to be converted from signed 16bit to unsigned
 * @author Phil Schatzmann
 * @copyright GPLv3
 * 
 * @tparam T 
 */
template<typename T>
class ConverterToInternalDACFormat : public  BaseConverter<T> {
    public:
        ConverterToInternalDACFormat(){
        }

        void convert(T (*src)[2], size_t size) {
            for (int i=0; i<size; i++) {
                src[i][0] = src[i][0] + 0x8000;
                src[i][1] = src[i][1] + 0x8000;
            }
        }
};

/**
 * @brief Combines multiple converters
 * 
 * @tparam T 
 */
template<typename T>
class MultiConverter : public BaseConverter<T> {
    public:
        MultiConverter(){
        }

        // adds a converter
        void add(BaseConverter<T> &converter){
            converters.push_back(converter);
        }

        // The data is provided as int24_t tgt[][2] but  returned as int24_t
        void convert(T (*src)[2], size_t size) {
            for(int i=0; i < converters.size(); i++){
                converters[i].convert(src);
            }
        }

    private:
        Vector<T> converters;

};

/**
 * @brief Converts e.g. 24bit data to the indicated bigger data type
 * @author Phil Schatzmann
 * @copyright GPLv3
 * 
 * @tparam T 
 */
template<typename FromType, typename ToType>
class CallbackConverter {
    public:
        CallbackConverter(ToType (*converter)(FromType v)){
            this->convert_ptr = converter;
        }

        // The data is provided as int24_t tgt[][2] but  returned as int24_t
        void convert(FromType src[][2], ToType target[][2], size_t size) {
            for (int i=size; i>0; i--) {
                target[i][0] = (*convert_ptr)(src[i][0]);
                target[i][1] = (*convert_ptr)(src[i][1]);
            }
        }

    private:
        ToType (*convert_ptr)(FromType v);

};



}