/*
 * ir_DistanceProtocol.hpp
 *
 * This decoder tries to decode a pulse width or pulse distance protocol.
 * 1. Analyze all space and mark length
 * 2. Decide if we have an pulse width or distance protocol
 * 3. Try to decode with the mark and space data found in step 1
 * No data and address decoding, only raw data as result.
 *
 * Pulse distance data can be sent with the generic function:
 * void sendPulseDistanceWidthData(unsigned int aOneMarkMicros, unsigned int aOneSpaceMicros, unsigned int aZeroMarkMicros,
 *            unsigned int aZeroSpaceMicros, uint32_t aData, uint8_t aNumberOfBits, bool aMSBfirst, bool aSendStopBit = false)
 * The header must be sent manually with:
 *          IrSender.mark(MarkMicros)
 *          IrSender.space(SpaceMicros);
 * see also: SendDemo example line 150
 *
 *  This file is part of Arduino-16tonIRremote.https://github.com/Arduino-IRremote/Arduino-IRremote.
 *
 ************************************************************************************
 * MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 ************************************************************************************
 */
#ifndef IR_DISTANCE_HPP
#define IR_DISTANCE_HPP

#include <Arduino.h>

// accept durations up to 50 * 50 (MICROS_PER_TICK) 2500 microseconds
#define DURATION_ARRAY_SIZE 50

// Switch the decoding according to your needs
#define DISTANCE_DO_MSB_DECODING PROTOCOL_IS_LSB_FIRST // this results in the same decodedRawData as e.g. the NEC and Kaseikyo/Panasonic decoder
//#define DISTANCE_DO_MSB_DECODING PROTOCOL_IS_MSB_FIRST // this resembles the JVC, Denon

//#define INFO // Activate this to save program space and suppress info output.
//#define DEBUG // Activate this for lots of lovely debug output from this decoder.
#include "IRremoteInt.h" // evaluates the DEBUG for IR_DEBUG_PRINT
//#include "LongUnion.h"

/** \addtogroup Decoder Decoders and encoders for different protocols
 * @{
 */
// see: https://www.mikrocontroller.net/articles/IRMP_-_english#Codings
#if defined(DEBUG)
void printDurations(uint8_t aArray[], uint8_t aMaxIndex) {
    for (uint_fast8_t i = 0; i <= aMaxIndex; i++) {
        if (i % 10 == 0) {
            if (i == 0) {
                Serial.print(' '); // indentation for the 0
            } else {
                Serial.println();
            }
            Serial.print(i);
            Serial.print(F(":"));
        }
        Serial.print(F(" | "));
        Serial.print(aArray[i]);
    }
    Serial.println();
}
#endif

bool aggregateArrayCounts(uint8_t aArray[], uint8_t aMaxIndex, uint8_t *aShortIndex, uint8_t *aLongIndex) {
    uint8_t tSum = 0;
    uint16_t tWeightedSum = 0;
    for (uint_fast8_t i = 0; i <= aMaxIndex; i++) {
        uint8_t tCurrentDurations = aArray[i];
        if (tCurrentDurations != 0) {
            // Add it to sum and remove array content
            tSum += tCurrentDurations;
            tWeightedSum += (tCurrentDurations * i);
            aArray[i] = 0;
        }
        if ((tCurrentDurations == 0 || i == aMaxIndex) && tSum != 0) {
            // here we have a sum and a gap after the values
            uint8_t tAggregateIndex = (tWeightedSum + (tSum / 2)) / tSum; // with rounding
            aArray[tAggregateIndex] = tSum; // disabling this line increases code size by 2 - unbelievable!
            // store aggregate for later decoding
            if (*aShortIndex == 0) {
                *aShortIndex = tAggregateIndex;
            } else if (*aLongIndex == 0) {
                *aLongIndex = tAggregateIndex;
            } else {
                // we have 3 bins => this is likely no pulse width or distance protocol. e.g. it can be RC5.
                return false;
            }
            // initialize for next aggregation
            tSum = 0;
            tWeightedSum = 0;
        }
    }
    return true;
}

/*
 * Try to decode a pulse width or pulse distance protocol.
 * 1. Analyze all space and mark length
 * 2. Decide if we have an pulse width or distance protocol
 * 3. Try to decode with the mark and space data found in step 1
 * No data and address decoding, only raw data as result.
 */
bool IRrecv::decodeDistance() {
    uint8_t tDurationArray[DURATION_ARRAY_SIZE];

    /*
     * Accept only protocols with at least 8 bits
     */
    if (decodedIRData.rawDataPtr->rawlen < (2 * 8) + 4) {
        IR_DEBUG_PRINT("PULSE_DISTANCE: ");
        IR_DEBUG_PRINT("Data length=");
        IR_DEBUG_PRINT(decodedIRData.rawDataPtr->rawlen);
        IR_DEBUG_PRINTLN(" is less than 20");
        return false;
    }

    uint_fast8_t i;
// Reset array
    memset(tDurationArray, 0, sizeof(tDurationArray));

    uint8_t tMaxDurationIndex = 0;
    // Count space durations. Skip leading start and trailing stop bit.
    for (i = 4; i < (uint_fast8_t) decodedIRData.rawDataPtr->rawlen - 2; i += 2) {
        uint8_t tDurationTicks = decodedIRData.rawDataPtr->rawbuf[i];
        if (tDurationTicks < sizeof(tDurationArray)) {
            tDurationArray[tDurationTicks]++;
            if (tMaxDurationIndex < tDurationTicks) {
                tMaxDurationIndex = tDurationTicks;
            }
        }
    }

// aggregate counts to one duration bin
    uint8_t tSpaceTicksShort = 0;
    uint8_t tSpaceTicksLong = 0;
    if (!aggregateArrayCounts(tDurationArray, tMaxDurationIndex, &tSpaceTicksShort, &tSpaceTicksLong)) {
        IR_DEBUG_PRINT(F("PULSE_DISTANCE: "));
        IR_DEBUG_PRINTLN(F("Space aggregation failed, more than 2 distinct duration values found"));
        return false;
    }

#if defined(DEBUG)
    Serial.println(F("Space:"));
    printDurations(tDurationArray, tMaxDurationIndex);
#endif

    // Reset array
    memset(tDurationArray, 0, sizeof(tDurationArray));

    tMaxDurationIndex = 0;
    // Count mark durations. Skip leading start and trailing stop bit.
    for (i = 3; i < (uint_fast8_t) decodedIRData.rawDataPtr->rawlen - 2; i += 2) {
        uint8_t tDurationTicks = decodedIRData.rawDataPtr->rawbuf[i];
        if (tDurationTicks < sizeof(tDurationArray)) {
            tDurationArray[tDurationTicks]++;
            if (tMaxDurationIndex < tDurationTicks) {
                tMaxDurationIndex = tDurationTicks;
            }
        }
    }

    uint8_t tMarkTicksShort = 0;
    uint8_t tMarkTicksLong = 0;
    if (!aggregateArrayCounts(tDurationArray, tMaxDurationIndex, &tMarkTicksShort, &tMarkTicksLong)) {
        IR_DEBUG_PRINT(F("PULSE_DISTANCE: "));
        IR_DEBUG_PRINTLN(F("Mark aggregation failed, more than 2 distinct duration values found"));
    }

#if defined(DEBUG)
    Serial.println(F("Mark:"));
    printDurations(tDurationArray, tMaxDurationIndex);
#endif
    // skip leading start and trailing stop bit.
    uint16_t tNumberOfBits = (decodedIRData.rawDataPtr->rawlen / 2) - 2;
    uint8_t tStartIndex = 3;
    decodedIRData.numberOfBits = tNumberOfBits;
    uint8_t tNumberOfAdditionalLong = (tNumberOfBits - 1) / 32;

    /*
     * decide, if we have an pulse width or distance protocol
     */
    if (tSpaceTicksLong == 0) {
        if (tMarkTicksLong == 0) {
            IR_DEBUG_PRINT(F("PULSE_DISTANCE: "));
            IR_DEBUG_PRINTLN(F("Only 1 distinct duration value for each space and mark found"));
            return false;
        }
//        // check if last bit can be decoded as data or not, in this case take it as a stop bit
//        if (decodePulseWidthData(1, decodedIRData.rawDataPtr->rawlen - 3, tMarkTicksLong * MICROS_PER_TICK,
//                tMarkTicksShort * MICROS_PER_TICK, tSpaceTicksShort * MICROS_PER_TICK, DISTANCE_DO_MSB_DECODING)) {
//            tNumberOfBits++;
//        }
        // decode without leading start bit. Currently only seen for sony protocol
        for (uint_fast8_t i = 0; i <= tNumberOfAdditionalLong; ++i) {
            uint8_t tNumberOfBitsForOneDecode = tNumberOfBits;
            if (tNumberOfBitsForOneDecode > 32) {
                tNumberOfBitsForOneDecode = 32;
            }
            if (!decodePulseWidthData(tNumberOfBitsForOneDecode, tStartIndex, tMarkTicksLong * MICROS_PER_TICK,
                    tMarkTicksShort * MICROS_PER_TICK, tSpaceTicksShort * MICROS_PER_TICK, DISTANCE_DO_MSB_DECODING)) {
                IR_DEBUG_PRINT(F("PULSE_WIDTH: "));
                IR_DEBUG_PRINTLN(F("Decode failed"));
                return false;
            }
            if (i == 0) {
                // Print protocol timing and length only once
                IR_INFO_PRINTLN();
                IR_INFO_PRINT(F("PULSE_WIDTH:"));
                IR_INFO_PRINT(F(" HeaderMarkMicros="));
                IR_INFO_PRINT(decodedIRData.rawDataPtr->rawbuf[1] * MICROS_PER_TICK);
                IR_INFO_PRINT(F(" HeaderSpaceMicros="));
                IR_INFO_PRINT(decodedIRData.rawDataPtr->rawbuf[2] * MICROS_PER_TICK);
                IR_INFO_PRINT(F(" OneMarkMicros="));
                IR_INFO_PRINT(tMarkTicksLong * MICROS_PER_TICK);
                IR_INFO_PRINT(F(" ZeroMarkMicros="));
                IR_INFO_PRINT(tMarkTicksShort * MICROS_PER_TICK);
                IR_INFO_PRINT(F(" SpaceMicros="));
                IR_INFO_PRINT(tSpaceTicksShort * MICROS_PER_TICK);
                IR_INFO_PRINT(F(" NumberOfBits="));
                IR_INFO_PRINT(decodedIRData.numberOfBits);
                IR_INFO_PRINT(F(" DecodedRawData:"));

            }
            IR_INFO_PRINT(F(" 0x"));
            IR_INFO_PRINT(decodedIRData.decodedRawData, HEX);
            tStartIndex += 64;
            tNumberOfBits -= 32;
        }
        IR_INFO_PRINTLN();

        // Store ticks used for decoding in extra
        decodedIRData.extra = (tMarkTicksShort << 8) | tMarkTicksLong;
        decodedIRData.protocol = PULSE_WIDTH;
    } else {
//        // check if last bit can be decoded as data or not, in this case take it as a stop bit
//        if (decodePulseDistanceData(1, decodedIRData.rawDataPtr->rawlen - 3, tMarkTicksShort * MICROS_PER_TICK,
//                tSpaceTicksLong * MICROS_PER_TICK, tSpaceTicksShort * MICROS_PER_TICK, DISTANCE_DO_MSB_DECODING)) {
//            Serial.print(F("tNumberOfBits++ "));
//            tNumberOfBits++;
//        }

        /*
         * Decode in 32 bit chunks
         */
        for (uint_fast8_t i = 0; i <= tNumberOfAdditionalLong; ++i) {
            uint8_t tNumberOfBitsForOneDecode = tNumberOfBits;
            if (tNumberOfBitsForOneDecode > 32) {
                tNumberOfBitsForOneDecode = 32;
            }
            if (!decodePulseDistanceData(tNumberOfBitsForOneDecode, tStartIndex, tMarkTicksShort * MICROS_PER_TICK,
                    tSpaceTicksLong * MICROS_PER_TICK, tSpaceTicksShort * MICROS_PER_TICK, DISTANCE_DO_MSB_DECODING)) {
                IR_DEBUG_PRINT(F("PULSE_DISTANCE: "));
                IR_DEBUG_PRINTLN(F("Decode failed"));
                return false;
            } else {
                if (i == 0) {
                    // Print protocol timing only once
                    IR_INFO_PRINTLN();
                    IR_INFO_PRINT(F("PULSE_DISTANCE:"));
                    IR_INFO_PRINT(F(" HeaderMarkMicros="));
                    IR_INFO_PRINT(decodedIRData.rawDataPtr->rawbuf[1] * MICROS_PER_TICK);
                    IR_INFO_PRINT(F(" HeaderSpaceMicros="));
                    IR_INFO_PRINT(decodedIRData.rawDataPtr->rawbuf[2] * MICROS_PER_TICK);
                    IR_INFO_PRINT(F(" MarkMicros="));
                    IR_INFO_PRINT(tMarkTicksShort * MICROS_PER_TICK);
                    IR_INFO_PRINT(F(" OneSpaceMicros="));
                    IR_INFO_PRINT(tSpaceTicksLong * MICROS_PER_TICK);
                    IR_INFO_PRINT(F(" ZeroSpaceMicros="));
                    IR_INFO_PRINT(tSpaceTicksShort * MICROS_PER_TICK);
                    IR_INFO_PRINT(F(" NumberOfBits="));
                    IR_INFO_PRINT(decodedIRData.numberOfBits);
                    IR_INFO_PRINT(F(" DecodedRawData:"));
                }
                IR_INFO_PRINT(F(" 0x"));
                IR_INFO_PRINT(decodedIRData.decodedRawData, HEX);
                tStartIndex += 64;
                tNumberOfBits -= 32;
            }
            IR_INFO_PRINTLN();
        }

        // Store ticks used for decoding in extra
        decodedIRData.extra = (tSpaceTicksShort << 8) | tSpaceTicksLong;
        decodedIRData.protocol = PULSE_DISTANCE;
    }

    if (DISTANCE_DO_MSB_DECODING) {
        decodedIRData.flags = IRDATA_FLAGS_IS_MSB_FIRST;
    }

    return true;
}

/** @}*/
#endif
#pragma once
