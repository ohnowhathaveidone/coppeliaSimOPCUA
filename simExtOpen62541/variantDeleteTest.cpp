#include <iostream>
#include <vector> 

extern "C" {
    #include <open62541/client_config_default.h>
    #include <open62541/client_highlevel.h>
    #include <open62541/client_subscriptions.h>
    #include <open62541/plugin/log_stdout.h>
}

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
    std::vector<UA_UInt32> arrayDims(dimensions.size());
    std::copy(dimensions.begin(), dimensions.end(), arrayDims.begin());
    
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
    value->arrayDimensions = reinterpret_cast<UA_UInt32 *>(arrayDims.data());
    
    //write
    UA_StatusCode retval = UA_Client_writeValueAttribute(client, id, value);
    
    //cleanup. 
    //free(value); //-> really?..
    //UA_Variant_delete(value);

    if (retval == UA_STATUSCODE_GOOD) {
        return 0;
    } else {
        return -1;
    }
}

int main (int argc, char *argv[]) {
    std::vector<float> inData;
    std::vector<int> dimensions {2, 2};

    //populate inData 
    for (int i=0; i<argc; i++){
        std::cout<<argv[i]<<std::endl;
        inData.push_back(atof(argv[i]));
    }

    UA_Client *client = UA_Client_new();
    UA_ClientConfig_setDefault(UA_Client_getConfig(client));
    UA_StatusCode success = UA_Client_connect(client, "opc.tcp://robstation.bfh.ch:4840");
    
    std::string nodeName = "double.matrix";
    UA_NodeId nodeId = UA_NODEID_STRING(1, (char*) nodeName.c_str());

    int retval = genericWriteNumericArray<UA_Double, float>(client, 
                                                            nodeId, 
                                                            inData, 
                                                            dimensions, 
                                                            &UA_TYPES[UA_TYPES_DOUBLE]);
    std::cout<<retval<<std::endl;

    //disconnect
    UA_Client_disconnect(client);
    UA_Client_delete(client);

    delete(&inData); //-> causes double free error
    return 0;
}