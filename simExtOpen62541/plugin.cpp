// Copyright 2016 Coppelia Robotics GmbH. All rights reserved. 
// marc@coppeliarobotics.com
// www.coppeliarobotics.com
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// -------------------------------------------------------------------
// Authors:
// Federico Ferri <federico.ferri.it at gmail dot com> - original plugin skeleton
// Niki Aigner <niki.aigner at gmail dot com> - mangled open62541 into this
// -------------------------------------------------------------------


//NOTES ON USAGE
//Currently, this plugin assumes that you know, what you're reading and what you're writing. 
//The way requests fail is by setting the success flag to -1, without explanation
//Before actually talking to a server, use a client w/ browsing capabilities to figure out, 
//what data to send/receive to/from where. 

#include "plugin.h"
#include "simPlusPlus/Plugin.h"
#include "stubs.h"
#include "simPlusPlus/Handle.h"

#include <iostream>

extern "C" {
    #include <open62541/client_config_default.h>
    #include <open62541/client_highlevel.h>
    #include <open62541/client_subscriptions.h>
    #include <open62541/plugin/log_stdout.h>
}

//set up - stuff for handle management
std::set<std::string> handles;

//def templates 
//template <> std::string Handle<TYPE>::tag() {return "TYPETAG"; } 
template <> std::string sim::Handle<UA_Client>::tag() {return "UA_Client"; } 

//Metadata - also useful
//in b0, this retreives user data. do i need this?..
//config is set differently for the UA thingy
/*
struct Metadata {
    std::string handle;
    //...
}
*/


//########################################################################//
//########################################################################//
//HELPER FUNCTIONS #######################################################//
//########################################################################//
//########################################################################//

//retreive data from node - works by string/numeric ID and GUID
//TODO: add error catching?


//overloaded nodeID
UA_NodeId genericNodeID(UA_UInt16 ns, char* id) {
    return UA_NODEID_STRING(ns, id);
}

UA_NodeId genericNodeID(UA_UInt16 ns, UA_UInt32 id) {
    return UA_NODEID_NUMERIC(ns, id);
}

UA_NodeId genericNodeID(UA_UInt16 ns, UA_Guid id) {
    return UA_NODEID_GUID(ns, id);
}

//pull value attribute
UA_Variant readNodeValue (std::string handle, UA_NodeId nodeId, int& success) {
    UA_Client *client = sim::Handle<UA_Client>::obj(handle);
    UA_Variant value;
    UA_Variant_init(&value);
    UA_StatusCode retval = UA_Client_readValueAttribute(client, nodeId, &value);
    
    if (retval == UA_STATUSCODE_GOOD){
        success = 0;
        return value;
    } else {
        success = (int) retval;
        return value;
    }
}


//TODO: ADD EXPANDEDNODEIDs?..
//fuck expanded IDs for now. 

//extract data from a UA_Variant - single int, single float, int array, float array
//the whole idea of relatively strict types when passing data to lua seems a bit off. 
//lua only knows stuff like number, string, bool, ... and is dynamically typed. 

//TODO: does what i'm doing here really make any sense?

void readSingleInt(UA_Variant value, int& output, int& success) {
    
    if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_UINT16])) {
        output = *(uint16_t *) value.data;
    } else if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_UINT32])) {
        output = *(uint32_t *) value.data;
    } else if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_UINT64])) {
        output = *(uint64_t *) value.data;
    } else if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_INT16])) {
        output = *(int16_t *) value.data;
    } else if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_INT32])) {
        output = *(int32_t *) value.data;
    } else if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_INT64])) {
        output = *(int64_t *) value.data;
    } else if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_BYTE])) {
        output = *(uint8_t *) value.data;
    } else if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_SBYTE])) {
        output = *(int8_t *) value.data;
    } else {
        output = 0;
        success |= 0x00000001; //indicate that something went wrong while keeping the original error code
    }
}

void readSingleFloat(UA_Variant value, float& output, int& success) {
    
    output = *(float *) value.data;
    if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_FLOAT])) {
        output = *(float *) value.data;
    } else if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_DOUBLE])) {
        //this is prbably a shit idea - plugin passes a float.
        output = *(double *) value.data; //ugh?... really? 
    } else {
        output = 0.;
        success |= 0x00000001; //indicate that something went wrong while keeping the original error code
    }
    
}

void readSingleBool(UA_Variant value, bool& output, int& success) {
    
    if (UA_Variant_hasScalarType(&value, &UA_TYPES[UA_TYPES_BOOLEAN])) {
        output = *(bool *) value.data;
    } else {
        output = false;
        success |= 0x00000001; //indicate that something went wrong while keeping the original error code
    }
    
}

//############################################################
//NOTE TO SELF on vector handling here
//############################################################
//UA_Variant.value holds a (void *) pointer - this is first converted to an appropriate datatype
//and tmpVector of that type is constructed (going from *bufferType to *buffeType+arrayLength)
//finally, tmpVector is cast into the datatype to be passed on 
//this seems to be correct way to do it when interfacing C code, i. e. 
//getting a (void*) buffer into a c++ vector, according to: 
//https://bytefreaks.net/programming-2/converting-a-void-buffer-to-a-stdvector 

//TODO: use static_cast? - nah, stackoverflow says it's fine for numbers
//TODO[MUCHLATER]: template this into genericReadValue/genericReadArray?

void readIntVector(UA_Variant value, std::vector<int>& intVector, int& success) {
    
    if (UA_Variant_hasArrayType(&value, &UA_TYPES[UA_TYPES_UINT16])) {
        const uint16_t *uintBuffer = (uint16_t *) value.data;
        std::vector<uint16_t> tmpVector(uintBuffer, uintBuffer+value.arrayLength);
        intVector = std::vector<int>(tmpVector.begin(), tmpVector.end());
    } else if (UA_Variant_hasArrayType(&value, &UA_TYPES[UA_TYPES_UINT32])) {
        const uint32_t *uintBuffer = (uint32_t *) value.data;
        std::vector<uint32_t> tmpVector(uintBuffer, uintBuffer+value.arrayLength);
        intVector = std::vector<int>(tmpVector.begin(), tmpVector.end());
    } else if (UA_Variant_hasArrayType(&value, &UA_TYPES[UA_TYPES_UINT64])) {
        const uint64_t *uintBuffer = (uint64_t *) value.data;
        std::vector<uint64_t> tmpVector(uintBuffer, uintBuffer+value.arrayLength);
        intVector = std::vector<int>(tmpVector.begin(), tmpVector.end());
    } else if (UA_Variant_hasArrayType(&value, &UA_TYPES[UA_TYPES_INT16])) {
        const int16_t *intBuffer = (int16_t *) value.data;
        std::vector<int16_t> tmpVector(intBuffer, intBuffer+value.arrayLength);
        intVector = std::vector<int>(tmpVector.begin(), tmpVector.end());
    } else if (UA_Variant_hasArrayType(&value, &UA_TYPES[UA_TYPES_INT32])) {
        const int32_t *intBuffer = (int32_t *) value.data;
        std::vector<int32_t> tmpVector(intBuffer, intBuffer+value.arrayLength);
        intVector = std::vector<int>(tmpVector.begin(), tmpVector.end());
    } else if (UA_Variant_hasArrayType(&value, &UA_TYPES[UA_TYPES_INT64])) {
        const int64_t *intBuffer = (int64_t *) value.data;
        std::vector<int64_t> tmpVector(intBuffer, intBuffer+value.arrayLength);
        intVector = std::vector<int>(tmpVector.begin(), tmpVector.end());
    } else if (UA_Variant_hasArrayType(&value, &UA_TYPES[UA_TYPES_SBYTE])) {
        const int8_t *intBuffer = (int8_t *) value.data;
        std::vector<int8_t> tmpVector(intBuffer, intBuffer+value.arrayLength);
        intVector = std::vector<int>(tmpVector.begin(), tmpVector.end());
    } else if (UA_Variant_hasArrayType(&value, &UA_TYPES[UA_TYPES_BYTE])) {
        const uint8_t *intBuffer = (uint8_t *) value.data;
        std::vector<uint8_t> tmpVector(intBuffer, intBuffer+value.arrayLength);
        intVector = std::vector<int>(tmpVector.begin(), tmpVector.end());
    } else {
        intVector = std::vector<int>{0};
        success |= 0x00000001; //indicate that something went wrong while keeping the original error code
    }
    
}

void readFloatVector(UA_Variant value, std::vector<float>& floatVector, int& success) {

    if (UA_Variant_hasArrayType(&value, &UA_TYPES[UA_TYPES_FLOAT])) {
        const float *floatBuffer = (float *) value.data;
        floatVector = std::vector<float>(floatBuffer, floatBuffer+value.arrayLength);
    } else if (UA_Variant_hasArrayType(&value, &UA_TYPES[UA_TYPES_DOUBLE])) {
        const double *doubleBuffer = (double *) value.data;
        //seems to work too - i guess vector takes care of conversion...
        floatVector = std::vector<float>(doubleBuffer, doubleBuffer+value.arrayLength);
        //std::vector<double> doubleVector (doubleBuffer, doubleBuffer+value.arrayLength);
        //floatVector = std::vector<float>(doubleVector.begin(), doubleVector.end());
    } else {
        floatVector = std::vector<float>{0.};
        success |= 0x00000001; //indicate that something went wrong while keeping the original error code
    }  
}

void readBoolVector(UA_Variant value, std::vector<bool>& boolVector, int& success) {

    if (UA_Variant_hasArrayType(&value, &UA_TYPES[UA_TYPES_BOOLEAN])) {
        const bool *boolBuffer = (bool *) value.data;
        boolVector = std::vector<bool>(boolBuffer, boolBuffer+value.arrayLength);
    } else {
        boolVector = std::vector<bool>{false};
        success |= 0x00000001; //indicate that something went wrong while keeping the original error code
    } 
}

//helpers for wrting
//should save some typing... 
//write single value attribute
template <typename uaType, typename cppType>
inline int genericWriteNumericValue(UA_Client *client, UA_NodeId id, 
        cppType inData, const UA_DataType *outDataType){
    uaType data = static_cast<uaType>(inData);
    UA_Variant *value = UA_Variant_new();
    UA_Variant_setScalarCopy(value, &data, outDataType);
    
    UA_StatusCode retval = UA_Client_writeValueAttribute(client, id, value);
    UA_Variant_delete(value);

    return (int) retval; //<- works too
}

//write arrays
template <typename uaType, typename cppType>
inline int genericWriteNumericArray (UA_Client *client, UA_NodeId id, 
        std::vector<cppType> inData, std::vector<int> dimensions, 
        const UA_DataType *outDataType){
    
    //inData is vector<float> or vector<int> 
    //not sure if std::copy actually helps here. 
    //i want to have the casts made explicitly. this functions will also be used to cast
    //around between (un)signed ints of various lentgths. 
    std::vector<uaType> tmpVec (inData.size());
    std::copy(inData.begin(), inData.end(), tmpVec.begin()); 
    std::vector<UA_UInt32> arrayDims(dimensions.begin(), dimensions.end());
    //std::vector<UA_UInt32> arrayDims(dimensions.size());
    //std::copy(dimensions.begin(), dimensions.end(), arrayDims.begin());
    
    //make variant
    UA_Variant *value = UA_Variant_new();
    UA_Variant_init(value);
    //NOTE: DO setArrayCopy first AND THEN SET DIMENSIONS!
    //using setArrayCopy leaks memory
    UA_Variant_setArray(value, 
                        static_cast<void *>(tmpVec.data()), 
                        static_cast<size_t>(tmpVec.size()), 
                        outDataType);
    
    value->arrayDimensionsSize = static_cast<size_t>(arrayDims.size());
    value->arrayDimensions = static_cast<UA_UInt32 *>(&arrayDims[0]); //<- seems to work...
    //value->arrayDimensions = reinterpret_cast<UA_UInt32 *>(arrayDims[0]);//(arrayDims.data());

    //write
    UA_StatusCode retval = UA_Client_writeValueAttribute(client, id, value);
    //std::cout<<std::hex<<retval<<std::endl;
    
    //for some reason, this leads to double free or corruption. 
    //i can't reproduce this in a simplified example, though... 
    //the problem seems to happen somewhere outside of this function, 
    //but it does work for genericWriteNumericValue
    //this likely causes a memory leak, right?..
    //-> yes, this causes a memory leak
    //UA_Variant_clear(value);
    //UA_Variant_delete(value);

    //really, fuck this for now. 

    //UPDATE:
    //i have no clue, why this works. 
    //using free(value), instead of the UA-suppertd functions does not lead to a crash
    //and mem leaks don't seem to appear in valgrind anymore... 
    //also, use setArray and not setArrayCopy 
    tmpVec.clear();
    arrayDims.clear();
    free(value); //-> wtf

    return (int) retval;
}

//TODO: vector<bool> does weird things...
//      check if this actually works!
inline int genericWriteBoolArray (UA_Client *client, UA_NodeId id, 
        std::vector<bool> inData, std::vector<int> dimensions, 
        const UA_DataType *outDataType){
    
    std::vector<char> tmpVec (inData.size());
    std::copy(inData.begin(), inData.end(), tmpVec.begin()); 
    std::vector<UA_UInt32> arrayDims(dimensions.size());
    std::copy(dimensions.begin(), dimensions.end(), arrayDims.begin());

    //make variant
    UA_Variant *value = UA_Variant_new();
    UA_Variant_init(value);
    UA_Variant_setArray(value, 
                        static_cast<void *>(tmpVec.data()), 
                        static_cast<size_t>(tmpVec.size()), 
                        outDataType);
    
    value->arrayDimensionsSize = static_cast<size_t>(arrayDims.size());
    value->arrayDimensions = reinterpret_cast<UA_UInt32 *>(arrayDims.data());
    
    //write
    UA_StatusCode retval = UA_Client_writeValueAttribute(client, id, value);
    tmpVec.clear();
    arrayDims.clear();
    free(value); //-> wtf

    return (int) retval;
}

// browse contents of a node
// template t allow browsing by numeric and string id
// TODO: browse by guid?
// NOTE: no, don't wrap this into a template. 
//       i'll just wrap parts of into functions, and call them in the respective requests
//       the one difference will be generating the nodeID
//       this not quite as elegant, but i think i'll have to use constexpr otherwise
//       which is c++17 and (currently) not default on gcc

// see https://github.com/open62541/open62541/blob/58bd161557111847d068650eff5ad670a9aa0395/examples/client.c#L61 
// for reference

// generates a browse request, EXCEPT for setting the nodeID
UA_BrowseRequest getBrowseRequest() {
    UA_BrowseRequest bReq;
    UA_BrowseRequest_init(&bReq);
    bReq.requestedMaxReferencesPerNode = 0;
    bReq.nodesToBrowse = UA_BrowseDescription_new();
    bReq.nodesToBrowseSize = 1;
    bReq.nodesToBrowse[0].resultMask = UA_BROWSERESULTMASK_ALL; /* return everything */

    return bReq;
}

// return multidimensional vector - probably easier to parse later
// i _think_ i can return a table of tables to coppeliaSim
std::vector<std::string> getBrowseResponse (UA_Client* client, UA_BrowseRequest bReq) {
    UA_BrowseResponse bResp = UA_Client_Service_browse(client, bReq);
    std::vector<std::string> response;
    std::string respLine; 
    std::string delimiter = ";";
    for(size_t i = 0; i < bResp.resultsSize; ++i) {
        for(size_t j = 0; j < bResp.results[i].referencesSize; ++j) {
            // rest of the original code: 
            /*
            UA_ReferenceDescription *ref = &(bResp.results[i].references[j]);
            if(ref->nodeId.nodeId.identifierType == UA_NODEIDTYPE_NUMERIC) {
                printf("%-9d %-16d %-16.*s %-16.*s\n", ref->nodeId.nodeId.namespaceIndex,
                       ref->nodeId.nodeId.identifier.numeric, (int)ref->browseName.name.length,
                       ref->browseName.name.data, (int)ref->displayName.text.length,
                       ref->displayName.text.data);
            } else if(ref->nodeId.nodeId.identifierType == UA_NODEIDTYPE_STRING) {
                printf("%-9d %-16.*s %-16.*s %-16.*s\n", ref->nodeId.nodeId.namespaceIndex,
                       (int)ref->nodeId.nodeId.identifier.string.length,
                       ref->nodeId.nodeId.identifier.string.data,
                       (int)ref->browseName.name.length, ref->browseName.name.data,
                       (int)ref->displayName.text.length, ref->displayName.text.data);
            }
            */
            //populate response
            //structure: [[idType,ns,ID,browseName,displayName], [...], .., [...]]
            //@future me: use push_back() to append data.
            // set line 
            // std::to_string(whatever)
            UA_ReferenceDescription *ref = &(bResp.results[i].references[j]);
            if(ref->nodeId.nodeId.identifierType == UA_NODEIDTYPE_NUMERIC) {
                respLine = "NUM" + delimiter + 
                            std::to_string(ref->nodeId.nodeId.namespaceIndex) + delimiter + 
                            std::to_string(ref->nodeId.nodeId.identifier.numeric) + delimiter + 
                            std::string((char *) ref->browseName.name.data, (size_t)ref->browseName.name.length) + delimiter + 
                            std::string((char *) ref->displayName.text.data, (size_t)ref->displayName.text.length)
                            ;
            } else if(ref->nodeId.nodeId.identifierType == UA_NODEIDTYPE_STRING) {
                respLine = "STR" + delimiter +  
                            std::to_string(ref->nodeId.nodeId.namespaceIndex) + delimiter + 
                            std::string((char *) ref->nodeId.nodeId.identifier.string.data, (size_t) ref->nodeId.nodeId.identifier.string.length) + delimiter + 
                            std::string((char *) ref->browseName.name.data, (size_t)ref->browseName.name.length) + delimiter +
                            std::string((char *) ref->displayName.text.data, (size_t)ref->displayName.text.length)
                            ;
            }
            response.push_back(respLine);
        }
    }

    return response;
}


//########################################################################//
//########################################################################//
//THESE ARE THE ACTUAL PLUGIN FUNCTIONALITIES ############################//
//########################################################################//
//########################################################################//

//function structure: 
/*
void fName (SScriptCallBack *p, const char *cmd, fName_in *in, fName_out *out) {
    //stuff
}
*/

//TODO: ADD AUTHENTICATED CONNECTIONS! - check. 
//TODO: remove copy-pasta? 
void createClient(SScriptCallBack *p, const char *cmd, 
        createClient_in *in, createClient_out *out) {
    
    UA_Client *client = UA_Client_new();
    UA_ClientConfig *myConf = UA_Client_getConfig(client);
    UA_ClientConfig_setDefault(myConf); //UA_Client_getConfig(client));
    myConf->timeout = (UA_UInt32)in->timeout;
    UA_StatusCode success = UA_Client_connect(client, in->address.c_str());
    if (success != UA_STATUSCODE_GOOD) {
        UA_Client_delete(client);
        auto handle = "invalid"; //catches crash when deleteing invalid client
        out->handle = handle;

        handles.insert(handle);
    } else {
        auto handle = sim::Handle<UA_Client>::str(client); //string
        out->handle = handle;
    }
    out->success = success; 
}


void createClientUPW(SScriptCallBack *p, const char *cmd, 
        createClientUPW_in *in, createClientUPW_out *out) {
    
    UA_Client *client = UA_Client_new();
    UA_ClientConfig *myConf = UA_Client_getConfig(client);
    UA_ClientConfig_setDefault(myConf); //UA_Client_getConfig(client));
    myConf->timeout = (UA_UInt32)in->timeout;
    UA_StatusCode success = UA_Client_connect_username(client, in->address.c_str(), in->user.c_str(), in->password.c_str());
    if (success != UA_STATUSCODE_GOOD) {
        UA_Client_delete(client);
        auto handle = "invalid"; //catches crash when deleteing invalid client
        out->handle = handle;

        handles.insert(handle);
    } else {
        auto handle = sim::Handle<UA_Client>::str(client); //string
        out->handle = handle;
    }
    out->success = success; 
}
void destroyClient(SScriptCallBack *p, const char *cmd, 
        destroyClient_in *in, destroyClient_out *out) {
    
    if (in->handle != "invalid")  {
        UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
        UA_Client_disconnect(client);
        UA_Client_delete(client);
        out->success = EXIT_SUCCESS; //is int
    } else {
        out->success = 1; //
    }

}

//NOTE
//*(type *) -> cast pointer to type and dereference. 

//SINGLE VALUES
//BY STRING ID
void readStringValueByStringID(SScriptCallBack *p, const char *cmd, 
        readStringValueByStringID_in *in, readStringValueByStringID_out *out) {
    
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char*) in->id.c_str());
    UA_Variant value = readNodeValue((std::string) in->handle, nodeId, out->success);
    out->value = *(std::string *) value.data;
    //out->success = 0; //(int)retval;
    UA_Variant_clear(&value);
}

void readIntValueByStringID(SScriptCallBack *p, const char *cmd, 
        readIntValueByStringID_in *in, readIntValueByStringID_out *out) {
    
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char*) in->id.c_str());
    UA_Variant value = readNodeValue((std::string) in->handle, nodeId, out->success);
    readSingleInt(value, out->value, out->success);
    UA_Variant_clear(&value);
}

void readFloatValueByStringID(SScriptCallBack *p, const char *cmd, 
        readFloatValueByStringID_in *in, readFloatValueByStringID_out *out) {
    
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char*) in->id.c_str());
    UA_Variant value = readNodeValue((std::string) in->handle, nodeId, out->success);
    readSingleFloat(value, out->value, out->success);
    UA_Variant_clear(&value);
}

//BY NUMERIC ID
void readStringValueByNumericID(SScriptCallBack *p, const char *cmd, 
        readStringValueByNumericID_in *in, readStringValueByNumericID_out *out) {
    
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);
    UA_Variant value = readNodeValue((std::string) in->handle, nodeId, out->success);
    out->value = *(std::string *) value.data;
    out->success = 0; 
    UA_Variant_clear(&value);
}

void readIntValueByNumericID(SScriptCallBack *p, const char *cmd, 
        readIntValueByNumericID_in *in, readIntValueByNumericID_out *out) {
    
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);
    UA_Variant value = readNodeValue((std::string) in->handle, nodeId, out->success);
    readSingleInt(value, out->value, out->success);
    UA_Variant_clear(&value);
}

void readFloatValueByNumericID(SScriptCallBack *p, const char *cmd, 
        readFloatValueByNumericID_in *in, readFloatValueByNumericID_out *out) {
    
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);
    UA_Variant value = readNodeValue((std::string) in->handle, nodeId, out->success);
    readSingleFloat(value, out->value, out->success);
    UA_Variant_clear(&value);
}

//ARRAYS 
//BY STRING ID

void readStringArrayByStringID(SScriptCallBack *p, const char *cmd, 
        readStringArrayByStringID_in *in, readStringArrayByStringID_out *out) {
    
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char*) in->id.c_str());
    UA_Variant value = readNodeValue((std::string) in->handle, nodeId, out->success);
    const char *stringBuffer = (char *) value.data;
    out->value = static_cast<std::vector<std::string>> (stringBuffer, value.arrayLength);
    out->success = 0; //(int)retval;
    
    UA_Variant_clear(&value);
}

void readIntArrayByStringID(SScriptCallBack *p, const char *cmd, 
        readIntArrayByStringID_in *in, readIntArrayByStringID_out *out) {
    
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char*) in->id.c_str());
    UA_Variant value = readNodeValue((std::string) in->handle, nodeId, out->success);
    readIntVector(value, out->value, out->success);
    UA_Variant_clear(&value);
}

void readFloatArrayByStringID(SScriptCallBack *p, const char *cmd, 
        readFloatArrayByStringID_in *in, readFloatArrayByStringID_out *out) {
    
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char*) in->id.c_str());
    UA_Variant value = readNodeValue((std::string) in->handle, nodeId, out->success);
    readFloatVector(value, out->value, out->success);
    UA_Variant_clear(&value);
}

//BY NUMERIC ID
void readStringArrayByNumericID(SScriptCallBack *p, const char *cmd, 
        readStringArrayByNumericID_in *in, readStringArrayByNumericID_out *out) {
    
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);
    UA_Variant value = readNodeValue((std::string) in->handle, nodeId, out->success);
    const char *stringBuffer = (char *) value.data;
    out->value = static_cast<std::vector<std::string>> (stringBuffer, value.arrayLength);
    out->success = 0; 
    
    UA_Variant_clear(&value);
}

void readIntArrayByNumericID(SScriptCallBack *p, const char *cmd, 
        readIntArrayByNumericID_in *in, readIntArrayByNumericID_out *out) {
    
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);
    UA_Variant value = readNodeValue((std::string) in->handle, nodeId, out->success);
    readIntVector(value, out->value, out->success);
    UA_Variant_clear(&value);
}

void readFloatArrayByNumericID(SScriptCallBack *p, const char *cmd, 
        readFloatArrayByNumericID_in *in, readFloatArrayByNumericID_out *out) {
    
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);
    UA_Variant value = readNodeValue((std::string) in->handle, nodeId, out->success);
    readFloatVector(value, out->value, out->success);
    UA_Variant_clear(&value);
}

//write methods
//single int values by string ID
void writeInt16ValueByStringID(SScriptCallBack *p, const char *cmd, 
        writeInt16ValueByStringID_in *in, writeInt16ValueByStringID_out *out) {
    
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char*) in->id.c_str());
    
    out->success = genericWriteNumericValue<UA_Int16, int>(client, nodeId, in->value, &UA_TYPES[UA_TYPES_INT16]);
}

void writeInt32ValueByStringID(SScriptCallBack *p, const char *cmd, 
        writeInt32ValueByStringID_in *in, writeInt32ValueByStringID_out *out) {
    
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char*) in->id.c_str());
    
    out->success = genericWriteNumericValue<UA_Int32, int>(client, nodeId, in->value, &UA_TYPES[UA_TYPES_INT32]);
}

void writeInt64ValueByStringID(SScriptCallBack *p, const char *cmd, 
        writeInt64ValueByStringID_in *in, writeInt64ValueByStringID_out *out) {
    
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char*) in->id.c_str());
    
    out->success = genericWriteNumericValue<UA_Int64, int>(client, nodeId, in->value, &UA_TYPES[UA_TYPES_INT64]);
}

void writeUInt16ValueByStringID(SScriptCallBack *p, const char *cmd, 
        writeUInt16ValueByStringID_in *in, writeUInt16ValueByStringID_out *out) {
    
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char*) in->id.c_str());
    
    out->success = genericWriteNumericValue<UA_UInt16, int>(client, nodeId, in->value, &UA_TYPES[UA_TYPES_UINT16]);
}

void writeUInt32ValueByStringID(SScriptCallBack *p, const char *cmd, 
        writeUInt32ValueByStringID_in *in, writeUInt32ValueByStringID_out *out) {
    
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char*) in->id.c_str());
    
    out->success = genericWriteNumericValue<UA_UInt32, int>(client, nodeId, in->value, &UA_TYPES[UA_TYPES_UINT32]);
}

void writeUInt64ValueByStringID(SScriptCallBack *p, const char *cmd, 
        writeUInt64ValueByStringID_in *in, writeUInt64ValueByStringID_out *out) {
    
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char*) in->id.c_str());
    
    out->success = genericWriteNumericValue<UA_UInt64, int>(client, nodeId, in->value, &UA_TYPES[UA_TYPES_UINT64]);
}

//single int values by numeric ID
void writeInt16ValueByNumericID(SScriptCallBack *p, const char *cmd, 
        writeInt16ValueByNumericID_in *in, writeInt16ValueByNumericID_out *out) {
    
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);
    
    out->success = genericWriteNumericValue<UA_Int16, int>(client, nodeId, in->value, &UA_TYPES[UA_TYPES_INT16]);
}

void writeInt32ValueByNumericID(SScriptCallBack *p, const char *cmd, 
        writeInt32ValueByNumericID_in *in, writeInt32ValueByNumericID_out *out) {
    
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);
    
    out->success = genericWriteNumericValue<UA_Int32, int>(client, nodeId, in->value, &UA_TYPES[UA_TYPES_INT32]);
}

void writeInt64ValueByNumericID(SScriptCallBack *p, const char *cmd, 
        writeInt64ValueByNumericID_in *in, writeInt64ValueByNumericID_out *out) {
    
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);
    
    out->success = genericWriteNumericValue<UA_Int64, int>(client, nodeId, in->value, &UA_TYPES[UA_TYPES_INT64]);
}

void writeUInt16ValueByNumericID(SScriptCallBack *p, const char *cmd, 
        writeUInt16ValueByNumericID_in *in, writeUInt16ValueByNumericID_out *out) {
    
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);
    
    out->success = genericWriteNumericValue<UA_UInt16, int>(client, nodeId, in->value, &UA_TYPES[UA_TYPES_UINT16]);
}

void writeUInt32ValueByNumericID(SScriptCallBack *p, const char *cmd, 
        writeUInt32ValueByNumericID_in *in, writeUInt32ValueByNumericID_out *out) {
    
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);
    
    out->success = genericWriteNumericValue<UA_UInt32, int>(client, nodeId, in->value, &UA_TYPES[UA_TYPES_UINT32]);
}

void writeUInt64ValueByNumericID(SScriptCallBack *p, const char *cmd, 
        writeUInt64ValueByNumericID_in *in, writeUInt64ValueByNumericID_out *out) {
    
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);
    
    out->success = genericWriteNumericValue<UA_UInt64, int>(client, nodeId, in->value, &UA_TYPES[UA_TYPES_UINT64]);
}

//single float/double by string ID
void writeFloatValueByStringID(SScriptCallBack *p, const char *cmd, 
        writeFloatValueByStringID_in *in, writeFloatValueByStringID_out *out) {
    
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char *) in->id.c_str());
    
    out->success = genericWriteNumericValue<UA_Float, float>(client, nodeId, in->value, &UA_TYPES[UA_TYPES_FLOAT]);
}

void writeDoubleValueByStringID(SScriptCallBack *p, const char *cmd, 
        writeDoubleValueByStringID_in *in, writeDoubleValueByStringID_out *out) {
    
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char *) in->id.c_str());
    
    out->success = genericWriteNumericValue<UA_Double, float>(client, nodeId, in->value, &UA_TYPES[UA_TYPES_DOUBLE]);
}

//single float/double by numeric ID
void writeFloatValueByNumericID(SScriptCallBack *p, const char *cmd, 
        writeFloatValueByNumericID_in *in, writeFloatValueByNumericID_out *out) {
    
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);
    
    out->success = genericWriteNumericValue<UA_Float, float>(client, nodeId, in->value, &UA_TYPES[UA_TYPES_FLOAT]);
}

void writeDoubleValueByNumericID(SScriptCallBack *p, const char *cmd, 
        writeDoubleValueByNumericID_in *in, writeDoubleValueByNumericID_out *out) {
    
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);
    
    out->success = genericWriteNumericValue<UA_Double, float>(client, nodeId, in->value, &UA_TYPES[UA_TYPES_DOUBLE]);
}

//write arrays 
//by string ID
//int arrays
void writeInt16ArrayByStringID(SScriptCallBack *p, const char *cmd, 
        writeInt16ArrayByStringID_in *in, writeInt16ArrayByStringID_out *out) {
    //
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char *) in->id.c_str());

    out->success = genericWriteNumericArray<UA_Int16, int>(client, nodeId, in->value, in->dimensions, &UA_TYPES[UA_TYPES_INT16]);
}
void writeInt32ArrayByStringID(SScriptCallBack *p, const char *cmd, 
        writeInt32ArrayByStringID_in *in, writeInt32ArrayByStringID_out *out) {
    //
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char *) in->id.c_str());

    out->success = genericWriteNumericArray<UA_Int32, int>(client, nodeId, in->value, in->dimensions, &UA_TYPES[UA_TYPES_INT32]);
}
void writeInt64ArrayByStringID(SScriptCallBack *p, const char *cmd, 
        writeInt64ArrayByStringID_in *in, writeInt64ArrayByStringID_out *out) {
    //
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char *) in->id.c_str());

    out->success = genericWriteNumericArray<UA_Int64, int>(client, nodeId, in->value, in->dimensions, &UA_TYPES[UA_TYPES_INT64]);
}
 void writeUInt16ArrayByStringID(SScriptCallBack *p, const char *cmd, 
        writeUInt16ArrayByStringID_in *in, writeUInt16ArrayByStringID_out *out) {
    //
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char *) in->id.c_str());

    out->success = genericWriteNumericArray<UA_UInt16, int>(client, nodeId, in->value, in->dimensions, &UA_TYPES[UA_TYPES_UINT16]);
}
void writeUInt32ArrayByStringID(SScriptCallBack *p, const char *cmd, 
        writeUInt32ArrayByStringID_in *in, writeUInt32ArrayByStringID_out *out) {
    //
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char *) in->id.c_str());

    out->success = genericWriteNumericArray<UA_UInt32, int>(client, nodeId, in->value, in->dimensions, &UA_TYPES[UA_TYPES_UINT32]);
}
void writeUInt64ArrayByStringID(SScriptCallBack *p, const char *cmd, 
        writeUInt64ArrayByStringID_in *in, writeUInt64ArrayByStringID_out *out) {
    //
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char *) in->id.c_str());

    out->success = genericWriteNumericArray<UA_UInt64, int>(client, nodeId, in->value, in->dimensions, &UA_TYPES[UA_TYPES_UINT64]);
}

//float/double arrays
void writeFloatArrayByStringID(SScriptCallBack *p, const char *cmd, 
        writeFloatArrayByStringID_in *in, writeFloatArrayByStringID_out *out) {
    //
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char *) in->id.c_str());

    out->success = genericWriteNumericArray<UA_Float, float>(client, nodeId, in->value, in->dimensions, &UA_TYPES[UA_TYPES_FLOAT]);
 }


void writeDoubleArrayByStringID(SScriptCallBack *p, const char *cmd, 
        writeDoubleArrayByStringID_in *in, writeDoubleArrayByStringID_out *out) {
    //
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char *) in->id.c_str());

    out->success = genericWriteNumericArray<UA_Double, float>(client, nodeId, in->value, in->dimensions, &UA_TYPES[UA_TYPES_DOUBLE]);
}

//by numeric ID
//int arrays
void writeInt16ArrayByNumericID(SScriptCallBack *p, const char *cmd, 
        writeInt16ArrayByNumericID_in *in, writeInt16ArrayByNumericID_out *out) {
    //
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);

    out->success = genericWriteNumericArray<UA_Int16, int>(client, nodeId, in->value, in->dimensions, &UA_TYPES[UA_TYPES_INT16]);
}
void writeInt32ArrayByNumericID(SScriptCallBack *p, const char *cmd, 
        writeInt32ArrayByNumericID_in *in, writeInt32ArrayByNumericID_out *out) {
    //
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);

    out->success = genericWriteNumericArray<UA_Int32, int>(client, nodeId, in->value, in->dimensions, &UA_TYPES[UA_TYPES_INT32]);
}
void writeInt64ArrayByNumericID(SScriptCallBack *p, const char *cmd, 
        writeInt64ArrayByNumericID_in *in, writeInt64ArrayByNumericID_out *out) {
    //
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);

    out->success = genericWriteNumericArray<UA_Int64, int>(client, nodeId, in->value, in->dimensions, &UA_TYPES[UA_TYPES_INT64]);
}
 void writeUInt16ArrayByNumericID(SScriptCallBack *p, const char *cmd, 
        writeUInt16ArrayByNumericID_in *in, writeUInt16ArrayByNumericID_out *out) {
    //
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);

    out->success = genericWriteNumericArray<UA_UInt16, int>(client, nodeId, in->value, in->dimensions, &UA_TYPES[UA_TYPES_UINT16]);
}
void writeUInt32ArrayByNumericID(SScriptCallBack *p, const char *cmd, 
        writeUInt32ArrayByNumericID_in *in, writeUInt32ArrayByNumericID_out *out) {
    //
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);

    out->success = genericWriteNumericArray<UA_UInt32, int>(client, nodeId, in->value, in->dimensions, &UA_TYPES[UA_TYPES_UINT32]);
}
void writeUInt64ArrayByNumericID(SScriptCallBack *p, const char *cmd, 
        writeUInt64ArrayByNumericID_in *in, writeUInt64ArrayByNumericID_out *out) {
    //
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);

    out->success = genericWriteNumericArray<UA_UInt64, int>(client, nodeId, in->value, in->dimensions, &UA_TYPES[UA_TYPES_UINT64]);
}

//float/double arrays
void writeFloatArrayByNumericID(SScriptCallBack *p, const char *cmd, 
        writeFloatArrayByNumericID_in *in, writeFloatArrayByNumericID_out *out) {
    //
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);

    out->success = genericWriteNumericArray<UA_Float, float>(client, nodeId, in->value, in->dimensions, &UA_TYPES[UA_TYPES_FLOAT]);
 }


void writeDoubleArrayByNumericID(SScriptCallBack *p, const char *cmd, 
        writeDoubleArrayByNumericID_in *in, writeDoubleArrayByNumericID_out *out) {
    //
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);

    out->success = genericWriteNumericArray<UA_Double, float>(client, nodeId, in->value, in->dimensions, &UA_TYPES[UA_TYPES_DOUBLE]);
}

//bool handling
//read single values
void readBoolValueByStringID(SScriptCallBack *p, const char *cmd, 
        readBoolValueByStringID_in *in, readBoolValueByStringID_out *out) {
    //
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char*) in->id.c_str());
    UA_Variant value = readNodeValue((std::string) in->handle, nodeId, out->success);
    readSingleBool(value, out->value, out->success);
    UA_Variant_clear(&value);

}

void readBoolValueByNumericID(SScriptCallBack *p, const char *cmd, 
        readBoolValueByNumericID_in *in, readBoolValueByNumericID_out *out) {
    //
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);
    UA_Variant value = readNodeValue((std::string) in->handle, nodeId, out->success);
    readSingleBool(value, out->value, out->success);
    UA_Variant_clear(&value);

}
//write single values
void writeBoolValueByStringID(SScriptCallBack *p, const char *cmd, 
        writeBoolValueByStringID_in *in, writeBoolValueByStringID_out *out) {
    //
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char *) in->id.c_str());
    
    out->success = genericWriteNumericValue<UA_Boolean, bool>(client, nodeId, in->value, &UA_TYPES[UA_TYPES_BOOLEAN]);

}
void writeBoolValueByNumericID(SScriptCallBack *p, const char *cmd, 
        writeBoolValueByNumericID_in *in, writeBoolValueByNumericID_out *out) {
    //
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);
    
    out->success = genericWriteNumericValue<UA_Boolean, bool>(client, nodeId, in->value, &UA_TYPES[UA_TYPES_BOOLEAN]);

}
//read arrays
void readBoolArrayByStringID(SScriptCallBack *p, const char *cmd, 
        readBoolArrayByStringID_in *in, readBoolArrayByStringID_out *out) {
    //
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char*) in->id.c_str());
    UA_Variant value = readNodeValue((std::string) in->handle, nodeId, out->success);
    readBoolVector(value, out->value, out->success);
    UA_Variant_clear(&value);
}
void readBoolArrayByNumericID(SScriptCallBack *p, const char *cmd, 
        readBoolArrayByNumericID_in *in, readBoolArrayByNumericID_out *out) {
    //
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);
    UA_Variant value = readNodeValue((std::string) in->handle, nodeId, out->success);
    readBoolVector(value, out->value, out->success);
    UA_Variant_clear(&value);
    
}
//write arrays
void writeBoolArrayByStringID(SScriptCallBack *p, const char *cmd, 
        writeBoolArrayByStringID_in *in, writeBoolArrayByStringID_out *out) {
    //
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char *) in->id.c_str());

    out->success = genericWriteBoolArray(client, nodeId, in->value, in->dimensions, &UA_TYPES[UA_TYPES_BOOLEAN]);
}
void writeBoolArrayByNumericID(SScriptCallBack *p, const char *cmd, 
        writeBoolArrayByNumericID_in *in, writeBoolArrayByNumericID_out *out) {
    //
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);

    out->success = genericWriteBoolArray(client, nodeId, in->value, in->dimensions, &UA_TYPES[UA_TYPES_BOOLEAN]);
}


//byte/short handling
//read single values
void readBytelValueByStringID(SScriptCallBack *p, const char *cmd, 
        readByteValueByStringID_in *in, readByteValueByStringID_out *out) {
    //
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char*) in->id.c_str());
    UA_Variant value = readNodeValue((std::string) in->handle, nodeId, out->success);
    readSingleInt(value, out->value, out->success);
    UA_Variant_clear(&value);

}

void readByteValueByNumericID(SScriptCallBack *p, const char *cmd, 
        readByteValueByNumericID_in *in, readByteValueByNumericID_out *out) {
    //
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);
    UA_Variant value = readNodeValue((std::string) in->handle, nodeId, out->success);
    readSingleInt(value, out->value, out->success);
    UA_Variant_clear(&value);

}
//write single values
void writeByteValueByStringID(SScriptCallBack *p, const char *cmd, 
        writeByteValueByStringID_in *in, writeByteValueByStringID_out *out) {
    //
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char *) in->id.c_str());
    
    out->success = genericWriteNumericValue<UA_Byte, int>(client, nodeId, in->value, &UA_TYPES[UA_TYPES_BYTE]);

}
void writeByteValueByNumericID(SScriptCallBack *p, const char *cmd, 
        writeByteValueByNumericID_in *in, writeByteValueByNumericID_out *out) {
    //
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);
    
    out->success = genericWriteNumericValue<UA_Byte, int>(client, nodeId, in->value, &UA_TYPES[UA_TYPES_BYTE]);

}
//read arrays
void readByteArrayByStringID(SScriptCallBack *p, const char *cmd, 
        readByteArrayByStringID_in *in, readByteArrayByStringID_out *out) {
    //
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char*) in->id.c_str());
    UA_Variant value = readNodeValue((std::string) in->handle, nodeId, out->success);
    readIntVector(value, out->value, out->success);
    UA_Variant_clear(&value);
}
void readByteArrayByNumericID(SScriptCallBack *p, const char *cmd, 
        readByteArrayByNumericID_in *in, readByteArrayByNumericID_out *out) {
    //
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);
    UA_Variant value = readNodeValue((std::string) in->handle, nodeId, out->success);
    readIntVector(value, out->value, out->success);
    UA_Variant_clear(&value);
    
}
//write arrays
void writeByteArrayByStringID(SScriptCallBack *p, const char *cmd, 
        writeByteArrayByStringID_in *in, writeByteArrayByStringID_out *out) {
    //
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char *) in->id.c_str());

    out->success = genericWriteNumericArray<UA_Byte, int>(client, nodeId, in->value, in->dimensions, &UA_TYPES[UA_TYPES_BYTE]);
}
void writeByteArrayByNumericID(SScriptCallBack *p, const char *cmd, 
        writeByteArrayByNumericID_in *in, writeByteArrayByNumericID_out *out) {
    //
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);

    out->success = genericWriteNumericArray<UA_Byte, int>(client, nodeId, in->value, in->dimensions, &UA_TYPES[UA_TYPES_BYTE]);
}

//write string arrays
//writeStringArrayByNumericID
//writeStringArrayByStringID
//writeStringVAlueByStringID
//writeStringValueByNumeric ID
void writeStringArrayByStringID(SScriptCallBack *p, const char *cmd, 
        writeStringArrayByStringID_in *in, writeStringArrayByStringID_out *out) {
    //

}

void writeStringArrayByNumericID(SScriptCallBack *p, const char *cmd, 
        writeStringArrayByNumericID_in *in, writeStringArrayByNumericID_out *out) {
    //

}

void writeStringValueByStringID(SScriptCallBack *p, const char *cmd, 
        writeStringValueByStringID_in *in, writeStringValueByStringID_out *out) {
    // TODO: MUCH HACK
    char* c_arr = &in->value[0];
    UA_String tmpVal = UA_STRING(c_arr);

    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (char *) in->id.c_str());
    UA_Variant *variant = UA_Variant_new();
    UA_Variant_setScalarCopy(variant, &tmpVal, &UA_TYPES[UA_TYPES_STRING]);
    UA_StatusCode retval = UA_Client_writeValueAttribute(client, nodeId, variant);
    UA_Variant_delete(variant);
    out->success = (int) retval;
}

void writeStringValueByNumericID(SScriptCallBack *p, const char *cmd, 
        writeStringValueByNumericID_in *in, writeStringValueByNumericID_out *out) {
    // TODO: MUCH HACK
    char* c_arr = &in->value[0];
    UA_String tmpVal = UA_STRING(c_arr);

    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    UA_NodeId nodeId = genericNodeID((UA_UInt16) in->ns, (UA_UInt32) in->id);
    UA_Variant *variant = UA_Variant_new();
    UA_Variant_setScalarCopy(variant, &tmpVal, &UA_TYPES[UA_TYPES_STRING]);
    UA_StatusCode retval = UA_Client_writeValueAttribute(client, nodeId, variant);
    UA_Variant_delete(variant);
    out->success = (int) retval;
}

//node browsing
void browseByNumericID(SScriptCallBack *p, const char *cmd, 
        browseByNumericID_in *in, browseByNumericID_out *out) {
    //
    UA_BrowseRequest bReq = getBrowseRequest();
    bReq.nodesToBrowse[0].nodeId = UA_NODEID_NUMERIC((UA_UInt16) in->ns, (UA_UInt32) in->id);
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    out->response = getBrowseResponse(client, bReq);
    out->success = 0;
}

void browseByStringID(SScriptCallBack *p, const char *cmd, 
        browseByStringID_in *in, browseByStringID_out *out) {
    //
    UA_BrowseRequest bReq = getBrowseRequest();
    bReq.nodesToBrowse[0].nodeId = UA_NODEID_STRING((UA_UInt16) in->ns, (char *) in->id.c_str());
    UA_Client *client = sim::Handle<UA_Client>::obj(in->handle);
    out->response = getBrowseResponse(client, bReq);
    out->success = 0;
}


//leaving for backwards compatibility.. ;)
void test(SScriptCallBack *p, const char *cmd, test_in *in, test_out *out)
{
    // we compute the average of the values in 'c'
    out->x = 0.0;
    for(int i = 0; i < in->c.size(); ++i)
        out->x += in->c[i];
    out->x /= (float)in->c.size();

    // we can access other input parameters as usual:
    int a = in->a;
    std::string b = in->b;
    // ...
}

class Plugin : public sim::Plugin
{
public:
    void onStart()
    {
        if(!registerScriptStuff())
            throw std::runtime_error("script stuff initialization failed");

        simSetModuleInfo(PLUGIN_NAME, 0, "Wrapper for open62541 OPC UA implementation", 0);
        simSetModuleInfo(PLUGIN_NAME, 1, BUILD_DATE, 0);
    }
};

SIM_PLUGIN("Open62541Wrapper", 1, Plugin)
