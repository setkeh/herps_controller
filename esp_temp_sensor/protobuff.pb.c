/* Automatically generated nanopb constant definitions */
/* Generated by nanopb-0.3.9.3 at Sun Jun  2 03:09:28 2019. */

#include "protobuff.pb.h"

/* @@protoc_insertion_point(includes) */
#if PB_PROTO_HEADER_VERSION != 30
#error Regenerate this file with the current version of nanopb generator.
#endif



const pb_field_t EnvironmentMessage_fields[4] = {
    PB_FIELD(  1, STRING  , REQUIRED, CALLBACK, FIRST, EnvironmentMessage, DeviceID, DeviceID, 0),
    PB_FIELD(  2, INT32   , REQUIRED, STATIC  , OTHER, EnvironmentMessage, Humidity, DeviceID, 0),
    PB_FIELD(  3, INT32   , REQUIRED, STATIC  , OTHER, EnvironmentMessage, Temperature, Humidity, 0),
    PB_LAST_FIELD
};


/* @@protoc_insertion_point(eof) */
