/*
 * fflog.h
 *
 *  Created on: Apr 16, 2018
 *      Author: Jesse Wang
 */

#ifndef FFLOG_H_
#define FFLOG_H_

#include <stdio.h>

#define FFLOG_DEBUG_PRINT_ENABLED 1
#define FFLOG_PRINT_ENABLED 1

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "libff"
#define fflog_debug_print(...) do{if(FFLOG_DEBUG_PRINT_ENABLED){\
    char strbuf[1024];\
    int r = sprintf(strbuf, "%s:%d:%s(): ", __FILE__, __LINE__, __func__);\
    sprintf(strbuf+r, __VA_ARGS__);\
    __android_log_print(ANDROID_LOG_INFO,LOG_TAG,"%s", strbuf);}}while(0)

#define fflog_print(...) do{if(FFLOG_PRINT_ENABLED){\
    __android_log_print(ANDROID_LOG_INFO,LOG_TAG, __VA_ARGS__);}}while(0)
#else

#define fflog_debug_print(...) do{if(FFLOG_DEBUG_PRINT_ENABLED){\
    char strbuf[1024];\
    int r = sprintf(strbuf, "%s:%d:%s(): ", __FILE__, __LINE__, __func__);\
    sprintf(strbuf+r, __VA_ARGS__);\
    printf("%s", strbuf); fflush(stdout);}}while(0)

#define fflog_print(...) do{if(FFLOG_PRINT_ENABLED){\
    printf(__VA_ARGS__); fflush(stdout);}}while(0)
#endif

#endif /* FFLOG_H_ */
