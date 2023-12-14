# WARNING
THIS WILL PROBABLY NOT WORK OR EVEN COMPILE W/OUT BEING A PITA!

# simOpen62541
Plugin for V-REP/CoppeliaSim - that exposes OPC UA client capabilities provided by the open62541 package.   
Browsing and publish/subscribe is currently not supported.  
The package is functional, but coinsider it an alpha version.  

# Dependencies  
CoppeliaSim PluginSkeleton NG: https://github.com/CoppeliaRobotics/simExtPluginSkeletonNG  
CoppeliaSim libPlugin: https://github.com/CoppeliaRobotics/libPlugin  
OPC UA implementation: https://github.com/open62541/open62541  
boost: https://www.boost.org/ or install via your package manager  

# Integration open62541:  
- compile & install open62541 WITH AMALGAMATION -> produces open62541.c/.h  
- open62541.c has string to char* conversions -> this won't compile w/ g++  
- ```gcc -c open62541.c -fPIC -o open62541.o``` -> produces object file that can be linked in g++  
- copy open62541.c/.h/.o into ./open62541/  
- include headers from open62541 w/ ```extern "C" {}``` block  
- general compilation: ```g++ path/to/open62541.o myOpcUaApp.cpp -o myOpcUaApp```  
- for the plugin, use CMake (see below)

# Modifications to CMake Variables:    
- CoppeliaSim_DIR -> /path/to/libPlugin/cmake/  
- COPPELIASIM_ROOT_DIR -> Create a new attribute and point it to your CoppeliaSim installation.  

# Changes to CMakeLists.txt in simExtPLuginSkeletonNG  
- add OPEN62541OBJECTFILE attribute as filepath and point to open62541.o (typically in ./open62541/)  
- in CMakeLists.txt add:  
```
set(OBJS 
    ${OPEN62541OBJECTFILE}
)
```
add   ```${OBJS}``` into ```target_link_libraries(...)```  
- this resolves a problem with linking boost:  
add ```find_package(Boost COMPONENTS regex REQUIRED)``` after ```find_package(Boost REQUIRED)``` and finally, ```Boost::regex``` into ```target_link_libraries(...)```  
I'm not sure, if this is the correct solution, but it seems to do the job. The problem is that boost is included by open62541 and libPlugin. libPlugin will compile without explicit linking against boost::regex, but it will fail with missing symbol when calling it (either b/ c-style linking, or because boost::regex needs to be explicitly included and recompiled with a project). This did, however, once cause function redefinition issues.     

# Error/Status code handling  
Status codes from open62541 operations are passed to the Lua interface through the success return value - see https://open62541.org/doc/current/statuscodes.html for reference. However, the cast into Lua's double messes with them a little bit. Instead of 0xXXXX'0000 (where XXXX contains the actual status info), you get 0xFFFF'FFFF'XXXX'0000. Just ignore the upper 16 bit. For properly outputting the codes in lua, do the following (this seems to be the most elegant solution):  
```lua  
possibleInput, success = simOpen62541.someFunction(args);  
print(string.format('%x', success));  
```  
One addition was made: 0xFFFF'FFFF'XXXX'0001 will be returned when no appropriate data type was found in the variant (i. e. trying to read some integer from a string). If other errors occurred before, this will leave the information of the errocode intact.     

# Comments on implementation  
The plugin should not crash most of the time. If a read fails, zero(s)/empty string(s) and an error code are returned.  
One way to crash the plugin (and coppeliasim) though, is to fail creating a client connection and then to try writing to / reading from that inexisten client.   
For writing arrays, the exact dimensions _should_ be provided to the functions. The array length, however is not passed and the array can be of arbitrary length. Superfluous elements in the destination array will not be altered (no zero padding or anything is done on the incomng data). I still need to check what happens to arrays that are too long. For arrays on siemens servers, dims can be passed as an empty list {}.  
Looking at source, it will be obvious that I had not understood the concept of variants. This should be addressed with refactoring.  

# Note on importing the Robotics Companion Specification in SiOME  
- SiOME is Siemens' OPC UA modeling editor, which can be obtained free of charge from the Siemens Industry Mall (export restrictions may apply).  
- Dependency: DI Model (Devices)  
- Comment out tags ```<Documentation>...</Documentation>``` in *NodeSet2.xml. They break the import.  

# Acknowledgements  
This package was initially (2018-2020) developed during a project at the [Bern University of Applied Sciences](https://www.bfh.ch/ahb/) in collaboration with [Güdel AG](https://www.gudel.com/). Financial support was provided by the [Swiss Innovation Agency (Innosuisse)](https://www.innosuisse.ch/inno/en/home.html).  
