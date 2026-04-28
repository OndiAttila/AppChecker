#include <windows.h>
#include <psapi.h>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <limits> 
#include <map>
#include <fstream>
#include <sstream>
//#include <filesystem>

#if defined(max)
#undef max
#endif  // defined(max)
#if defined(min)
#undef min
#endif  // defined(min)

using namespace std::literals;

const size_t NSIZE = 1024;

struct Region {
    size_t size;
    const unsigned char* data;

    Region(size_t size, const unsigned char* data) :
        size(size), data(data)
    { }
};

// map-of-(imageName, pair-of-(enforceType, list-of-(region)))
std::map<std::string, std::pair<int, std::vector<Region> > > knownRegions;
std::map<std::string, std::pair<int, std::vector<Region> > > newRegions;
std::string outDirPath;

int checkProcess(
    DWORD processId,
    int defaultEnforceType,
    int forceEnforceType,
    const std::string& outDirPath,
    std::string& imageFileName,
    std::vector<std::string>& newRegionFiles)
{
//    std::cout << "Scanning process: " << processId << std::endl;

    //    DWORD dwDesiredAccess = PROCESS_ALL_ACCESS;
    DWORD dwDesiredAccess = PROCESS_VM_READ | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_TERMINATE;
    BOOL  bInheritHandle = FALSE;

    // ERRORS:
    //    5 (Access denied) -- Not enough privilege
    //   87 (Invalid parameter) -- process already exited
    HANDLE hProcess = ::OpenProcess(
        dwDesiredAccess,
        bInheritHandle,
        processId
        );
    if (hProcess == nullptr) {
        std::cerr << "ERROR: OpenProcess failed (id: " << processId << ") error: " << ::GetLastError() << std::endl;
        return 1;
    }

    char tmpImageFileName[NSIZE] = {};
/*
    DWORD imageFileNameLen = ::GetProcessImageFileNameA(
        hProcess,
        tmpImageFileName,
        NSIZE
        );
    if (imageFileNameLen == 0) {
        std::cerr << "ERROR: GetProcessImageFileNameA failed (id: " << processId << ") error: " << ::GetLastError() << std::endl;
        return 1;
    }
    std::cout << "Image file name: " << tmpImageFileName << std::endl;
*/
    DWORD imageFileNameLen = NSIZE;
    if (!::QueryFullProcessImageNameA(
        hProcess,
        0,
        tmpImageFileName,
        &imageFileNameLen
        )) {
        std::cerr << "ERROR: QueryFullProcessImageNameA failed (id: " << processId << ") error: " << ::GetLastError() << std::endl;
        return 1;
    }
    imageFileName = tmpImageFileName;
//    std::cout << "Image file name: " << imageFileName << std::endl;

/*
    HMODULE moduleHandles[NSIZE];
    DWORD sizeNeeded = 0;

    if (!::EnumProcessModules(
        hProcess,
        moduleHandles,
        NSIZE * sizeof(moduleHandles[0]),
        &sizeNeeded
        )) {
        std::cerr << "ERROR: EnumProcessModules failed; sizeNeeded: " << sizeNeeded << " (elems: " << (sizeNeeded / sizeof(moduleHandles[0])) << ")" << " (id: " << processId << ") error: " << ::GetLastError() << std::endl;
        return 1;
    }

//    DWORD countNeeded = sizeNeeded / sizeof(moduleHandles[0]);
    DWORD countNeeded = sizeNeeded / sizeof(moduleHandles[0]);
    if (countNeeded > NSIZE) {
        std::cerr << "ERROR: EnumProcessModules failed (buffer not big enough -- needed: " << countNeeded << " elems) (id: " << processId << ") error: " << ::GetLastError() << std::endl;
        return 1;
    }

//    const void* lpBaseAddress = 0;
//    DWORD sizeOfImage = 0;

    std::map<size_t, DWORD> modules;

    for (DWORD i = 0; i < countNeeded; ++i) {
//         char moduleName[NSIZE] = {};
//         DWORD moduleNameLen = ::GetModuleFileNameExA(
//             hProcess,
//             moduleHandles[i],
//             moduleName,
//             NSIZE
//             );
//         if (moduleNameLen == 0) {
//             std::cerr << "ERROR: GetModuleFileNameExA failed (id: " << processId << ") error: " << ::GetLastError() << std::endl;
//             continue;
//         }
//
//         if (imageFileName == moduleName) {
//             std::cout << "Skipping module: " << moduleName << std::endl;
//             continue;
//         }

        MODULEINFO moduleInfo = {};
        if (!::GetModuleInformation(
            hProcess,
            moduleHandles[i],
            &moduleInfo,
            sizeof(moduleInfo)
            )) {
            std::cerr << "ERROR: GetModuleInformation failed (id: " << processId << ") error: " << ::GetLastError() << std::endl;
            continue;
        }

//        lpBaseAddress = moduleInfo.lpBaseOfDll;
//        sizeOfImage = moduleInfo.SizeOfImage;

        modules[reinterpret_cast<const size_t>(moduleInfo.lpBaseOfDll)] = moduleInfo.SizeOfImage;
    }
*/

/*
//    unsigned char buffer[16*1024*1024] = {};
    const size_t BUFF_SIZE = 16*1024*1024;
    unsigned char* buffer = new unsigned char[BUFF_SIZE];
    if (lpBaseAddress == 0) {
        std::cerr << "ERROR: Could not obtain base module address (id: " << processId << ")" << std::endl;
        return 1;
    } else if (sizeOfImage > BUFF_SIZE) {
        std::cerr << "ERROR: Buffer not big enough (needed: " << sizeOfImage << ", got: " << sizeof(buffer) << ") (id: " << processId << ")" << std::endl;
        return 1;
    }

    SIZE_T nBytesRead = 0;

    if (!::ReadProcessMemory(
            hProcess,
            lpBaseAddress,
            buffer,
            sizeOfImage,
            &nBytesRead
            )) {
        std::cerr << "ERROR: ReadProcessMemory failed (id: " << processId << ") error: " << ::GetLastError() << std::endl;
        return 1;
    }
*/


    bool isBreach = false;
    int enforceType = -1;  // will be set later
std::cerr << "[DEBUG] starting enforceType: " << enforceType << " (id: " << processId << ")" << std::endl;


    MEMORY_BASIC_INFORMATION memInfo = {};
    unsigned char* startAddr = 0;
    size_t numRegionsScanned = 0;
    size_t numExecRegions = 0;
    SIZE_T sizeFilled = 0;
    while ((sizeFilled = ::VirtualQueryEx(
        hProcess,
        startAddr,
        &memInfo,
        sizeof(memInfo)
        )) != 0) {
        ++numRegionsScanned;
//        if ((numRegionsScanned % 1000) == 0) {
//            std::cout << "Processing (scanned: " << numRegionsScanned << " regions, startAddr: 0x" << std::hex << reinterpret_cast<const void*>(startAddr) << std::dec << ", regionSize: 0x" << std::hex << memInfo.RegionSize << std::dec << ")" << std::endl;
//        }

        if ((memInfo.Type == MEM_IMAGE) ||
            (memInfo.State == MEM_FREE) || (memInfo.State == MEM_RESERVE)) {
            startAddr += memInfo.RegionSize;
            continue;
        }
        if ((memInfo.AllocationProtect == PAGE_EXECUTE) || (memInfo.AllocationProtect == PAGE_EXECUTE_READ) || (memInfo.AllocationProtect == PAGE_EXECUTE_READWRITE) || (memInfo.AllocationProtect == PAGE_EXECUTE_WRITECOPY) ||
             (memInfo.Protect == PAGE_EXECUTE) || (memInfo.Protect == PAGE_EXECUTE_READ) || (memInfo.Protect == PAGE_EXECUTE_READWRITE) || (memInfo.Protect == PAGE_EXECUTE_WRITECOPY)) {
            ++numExecRegions;

            if (numExecRegions == 1) {
                std::cout << std::endl;
                std::cout << "PAGE_EXECUTE: 0x" << std::hex << PAGE_EXECUTE << std::dec << std::endl;
                std::cout << "PAGE_EXECUTE_READ: 0x" << std::hex << PAGE_EXECUTE_READ << std::dec << std::endl;
                std::cout << "PAGE_EXECUTE_READWRITE: 0x" << std::hex << PAGE_EXECUTE_READWRITE << std::dec << std::endl;
                std::cout << "PAGE_EXECUTE_WRITECOPY: 0x" << std::hex << PAGE_EXECUTE_WRITECOPY << std::dec << std::endl;
            }

            unsigned char* procRegionCopy = new unsigned char[memInfo.RegionSize];

            const unsigned char* readStartAddr = startAddr;
            const unsigned char* readEndAddr = startAddr + memInfo.RegionSize;
            unsigned char* writeStartAddr = procRegionCopy;
            while (readStartAddr < readEndAddr) {
                size_t writeBytesRemaining = procRegionCopy + memInfo.RegionSize - writeStartAddr;
                SIZE_T nBytesRead = 0;
                if (!::ReadProcessMemory(
                        hProcess,
                        readStartAddr,
                        writeStartAddr,
                        writeBytesRemaining,
                        &nBytesRead
                        )) {
                    std::cerr << "ERROR: ReadProcessMemory failed (id: " << processId << ") error: " << ::GetLastError() << std::endl;
                    return 1;
                }

                readStartAddr += nBytesRead;
                writeStartAddr += nBytesRead;
            }

            // check knownRegions
            bool isKnown = false;
            auto knownIter = knownRegions.find(imageFileName);
            if (knownIter != knownRegions.end()) {
                enforceType = knownIter->second.first;
std::cerr << "[DEBUG] got enforceType: " << enforceType << std::endl;
                for (const auto& elem : knownIter->second.second) {
                    if (elem.size == memInfo.RegionSize) {
                        if (std::memcmp(procRegionCopy, elem.data, memInfo.RegionSize) == 0) {
                            isKnown = true;
                            break;
                        }
                    }
                }
            }

            if (!isKnown) {
                std::cout << "Process " << processId << " (" << imageFileName << ") has unknown non-image EXEC mem region: 0x" << std::hex << memInfo.BaseAddress << std::dec << ", size: " << memInfo.RegionSize << std::endl;
                std::cout << "  AllocationProtect: 0x" << std::hex << memInfo.AllocationProtect << std::dec << std::endl;
                std::cout << "  Protect: 0x" << std::hex << memInfo.Protect << std::dec << std::endl;

                isBreach = true;

                bool isUnseen = true;
                auto newIter = newRegions.find(imageFileName);
                if (newIter != newRegions.end()) {
                    for (const auto& elem : newIter->second.second) {
                        if (elem.size == memInfo.RegionSize) {
                            if (std::memcmp(procRegionCopy, elem.data, memInfo.RegionSize) == 0) {
                                isUnseen = false;
                                break;
                            }
                        }
                    }
                }

                if (isUnseen) {
                    if (newIter == newRegions.end()) {
                        auto p = newRegions.insert({imageFileName, std::pair<int, std::vector<Region> >()});
                        newIter = p.first;
                    }
                    newIter->second.second.push_back({memInfo.RegionSize, procRegionCopy});

                    size_t outFileNameIdx = numExecRegions;
                    if (knownIter != knownRegions.end()) {
                        outFileNameIdx += knownIter->second.second.size();
                    }

                    std::string outFileName = imageFileName;
                    for (auto dataIter = outFileName.begin(); dataIter != outFileName.end(); ++dataIter) {
                        if ((*dataIter == '\\') || (*dataIter == ':')) {
                            *dataIter = '=';
                        }
                    }
                    outFileName += '_';
                    outFileName += ((outFileNameIdx < 100) ? '0' : ('0'+(outFileNameIdx/100)));
                    outFileName += ((outFileNameIdx < 10) ? '0' : ('0'+((outFileNameIdx/10)%10)));
                    outFileName += ('0'+(outFileNameIdx%10));
                    outFileName += ".bin";
                    std::string outFilePath = outDirPath;
                    if (outFilePath[outFilePath.size()-1] != '\\') {
                        outFilePath += '\\';
                    }
                    outFilePath += outFileName;

                    // create parent dirs
                    std::filesystem::create_directories(std::filesystem::path(outFilePath).parent_path());

                    FILE* outFile = std::fopen(outFilePath.c_str(), "wb");
                    if (outFile == nullptr) {
                        std::cerr << "ERROR: fopen failed (id: " << processId << ")" << std::endl;
                        return 1;
                    }

/*
                    const unsigned char* readStartAddr = startAddr;
                    const unsigned char* readEndAddr = startAddr + memInfo.RegionSize;
                    while (readStartAddr < readEndAddr) {
                        unsigned char buffer[NSIZE] = {};
                        SIZE_T nBytesRead = 0;
                        if (!::ReadProcessMemory(
                                hProcess,
                                readStartAddr,
                                buffer,
                                NSIZE,
                                &nBytesRead
                                )) {
                            std::cerr << "ERROR: ReadProcessMemory failed (id: " << processId << ") error: " << ::GetLastError() << std::endl;
                            std::fclose(outFile);
                            return 1;
                        }

                        fwrite(buffer, 1, nBytesRead, outFile);

                        readStartAddr += nBytesRead;
                    }
*/
                    fwrite(procRegionCopy, 1, memInfo.RegionSize, outFile);

                    std::fclose(outFile);


                    newRegionFiles.push_back(std::filesystem::path(outFilePath).filename().string());
//std::cout << "DEBUG: [checkProcess] newRegionFiles size: " << newRegionFiles.size() << std::endl;
                }

                int tmpEnforceType = ((forceEnforceType >= 0) ?
                    forceEnforceType :
                    ((enforceType >= 0) ?
                        enforceType :
                        defaultEnforceType));
                switch (tmpEnforceType) {
                    case 0: {
                        // do nothing

                        break;
                    }
                    case 1: {
                        DWORD newProtect = memInfo.Protect;
                        if (memInfo.Protect == PAGE_EXECUTE) {
                            newProtect = PAGE_READONLY;
                        }
                        if (memInfo.Protect == PAGE_EXECUTE_READ) {
                            newProtect = PAGE_READONLY;
                        }
                        if (memInfo.Protect == PAGE_EXECUTE_READWRITE) {
                            newProtect = PAGE_READWRITE;
                        }
                        if (memInfo.Protect == PAGE_EXECUTE_WRITECOPY) {
                            newProtect = PAGE_WRITECOPY;
                        }

                        if (newProtect != memInfo.Protect) {
                            if (::VirtualAllocEx(hProcess, startAddr, memInfo.RegionSize, MEM_COMMIT, newProtect) == nullptr) {
                                std::cerr << "ERROR: VirtualAllocEx failed (id: " << processId << ") error: " << ::GetLastError() << std::endl;
                            }
                        }

                        break;
                    }
                    case 2: {
                        // do no8thing; handled later

                        break;
                    }
                    default: {
                        throw std::runtime_error("Unknown enforceType value: " + std::to_string(tmpEnforceType));
                    }

                }
            }
        }

/*
        auto moduleIter = modules.lower_bound(memInfo.BaseAddress);
        if (moduleIter == modules.end() || moduleIter->first + moduleIter->second < memInfo.BaseAddress) {
            xxx;
        }
*/

        startAddr += memInfo.RegionSize;
    }
    if (numRegionsScanned == 0) {
        std::cerr << "ERROR: VirtualQueryEx failed (id: " << processId << ") error: " << ::GetLastError() << std::endl;
        return 1;
    }

    if (isBreach) {
std::cerr << "[DEBUG] breach!" << std::endl;
        int tmpEnforceType = ((forceEnforceType >= 0) ?
            forceEnforceType :
            ((enforceType >= 0) ?
                enforceType :
                defaultEnforceType));
std::cerr << "[DEBUG] using enforceType: " << tmpEnforceType << std::endl;
        switch (tmpEnforceType) {
            case 0: {
                // do nothing

                break;
            }
            case 1: {
                // do nothing; already handled

                break;
            }
            case 2: {
                std::cout << "Terminating process " << processId << "..." << std::endl;
                if (!::TerminateProcess(hProcess, 1)) {
                    std::cerr << "ERROR: TerminateProcess failed (id: " << processId << ") error: " << ::GetLastError() << std::endl;
                }

                break;
            }
            default: {
                throw std::runtime_error("Unknown enforceType value: " + std::to_string(tmpEnforceType));
            }
        }
    }

//    std::cout << "Scanned " << numRegionsScanned << " regions" << std::endl;

    return 0;
}

void readConfig(
    std::string configPath,
    // map-of-(imageName, pair-of-(enforcementType, list-of-(region)))
    std::map<std::string, std::pair<int, std::vector<Region> > >& regions,
    std::string& outDirPath)
{
    std::ifstream inFile(configPath.c_str(), std::ios::in);
    std::string line;
    while (std::getline(inFile, line)) {
        if ((line.size() == 0) || (line[0] == '#')) {
            continue;
        }
        std::string separator = ": {"s;
        auto sepIdx = line.find(separator);
        if (sepIdx != std::string::npos) {
            auto key = line.substr(0, sepIdx);
            auto val = line.substr(sepIdx+separator.size(), line.size()-(sepIdx+separator.size()));

            if (key == "region") {
                std::string imageName;
                int appEnforceType = -1;  // -1 - not set
                std::vector<std::string> regionFilePaths;
                while (std::getline(inFile, line)) {
                    if ((line.size() == 0) || (line[0] == '#')) {
                        continue;
                    } else if (line[0] == '}') {
                        break;  // 'region' section
                    }
                    auto sepIdx2 = line.find('=');
                    if (sepIdx2 != std::string::npos) {
                        auto key2 = line.substr(0, sepIdx2);
                        auto val2 = line.substr(sepIdx2+1, line.size()-(sepIdx2+1));

                        if (key2 == "  image") {
                            if (val2.size() == 0) {
                                continue;
                            }
                            imageName = val2;
                        } else if (key2 == "  enforce") {
                            int tmp = std::stoi(val2);
                            if ((tmp >= 0) && (tmp <= 2)) {
                                appEnforceType = tmp;
                            }
                        } else if (key2 == "  path") {
                            if (val2.size() == 0) {
                                continue;
                            }
                            regionFilePaths.push_back(val2);
                        } else {
                            throw std::runtime_error("ERROR: Invalid configuration (invalid region section; unknown value: '" + line + "')");
                        }
                    }
                }

                if (imageName.size() == 0) {
                    throw std::runtime_error("ERROR: Invalid configuration (missing 'image' value in region section)");
                } else if (regionFilePaths.size() == 0) {
                    throw std::runtime_error("ERROR: Invalid configuration (missing 'path' value in region section)");
                }

                auto regionIter = regions.find(imageName);
                if (regionIter == regions.end()) {
                    auto p = regions.insert({imageName, std::pair<int, std::vector<Region> >()});
                    regionIter = p.first;
                }

                regionIter->second.first = ((appEnforceType >= 0) ? appEnforceType : -1);
                for (const auto& regionFilePath : regionFilePaths) {
                    // read region file
                    // TODO: calculate regionFile path relative to configPath
                    auto rngFilePath = std::filesystem::path(configPath).parent_path() / regionFilePath;
                    std::ifstream regionFile(rngFilePath.string(), std::ios::in | std::ios::binary | std::ios::ate);
                    if (regionFile.fail()) {
                        std::cerr << "Could not open region file: " << rngFilePath.string() << std::endl;
                        continue;
                    }
                    size_t regionSize = regionFile.tellg();
                    regionFile.seekg(0);
                    unsigned char* buffer = new unsigned char[regionSize];
                    regionFile.read(reinterpret_cast<char*>(buffer), regionSize);

                    regionIter->second.second.push_back({regionSize, buffer});
                }
            } else if (key == "out") {
                std::string tmpOutDirPath;
                while (std::getline(inFile, line)) {
                    if (line.size() == 0 || line[0] == '#') {
                        continue;
                    } else if (line[0] == '}') {
                        break;  // 'out' section
                    }
                    auto sepIdx2 = line.find('=');
                    if (sepIdx2 != std::string::npos) {
                        auto key2 = line.substr(0, sepIdx2);
                        auto val2 = line.substr(sepIdx2+1, line.size()-(sepIdx2+1));

                        if (key2 == "  path") {
                            tmpOutDirPath = val2;
                        } else {
                            throw std::runtime_error("ERROR: Invalid configuration (invalid out section: '" + line + "')");
                        }
                    }
                }

                if (tmpOutDirPath.size() == 0) {
                    throw std::runtime_error("ERROR: Invalid configuration (missing 'path' in out section)");
                }

                outDirPath = tmpOutDirPath;
            } else {
                throw std::runtime_error("ERROR: Invalid configuration (unknown section type: ''" + line + ")");
            }
        } else {
            throw std::runtime_error("ERROR: Invalid configuration (unknown section: ''" + line + ")");
        }
    }
}

int writeConfig(
    // map-of-(imageName, list-of-(regionPath))
    const std::map<std::string, std::vector<std::string> >& regionConfig,
    const std::string& outFilePath)
{
//std::cout << "DEBUG: [writeConfig] regionConfig size: " << regionConfig.size() << std::endl;
    if (regionConfig.size() == 0) {
        return 0;
    }

    std::ofstream outFile(outFilePath, std::ios::out);
    for (const auto& cfgEntry : regionConfig) {
        outFile << "region: {" << std::endl;
        outFile << "  image=" << cfgEntry.first << std::endl;
        for (const auto& path : cfgEntry.second) {
            outFile << "  path=" << path << std::endl;
        }
        outFile << "}" << std::endl;
    }

    return 0;
}

int performChecks(
    DWORD processId,
    int defaultEnforceType,
    int forceEnforceType,
    const std::string& outDirPath)
{
    std::string outSubDirPath = outDirPath;
    // add sub-dir path-entry
    std::string timeStr;
    {
        auto t = std::time(nullptr);
        auto tm = *std::localtime(&t);

        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H-%M-%S");
        timeStr = oss.str();
    }
    if (outSubDirPath[outSubDirPath.size()-1] != '\\') {
        outSubDirPath += '\\';
    }
    outSubDirPath += timeStr;

    // map-of-(imageName, list-of-(regionPath))
    std::map<std::string, std::vector<std::string> > newRegionConfig;

    if (processId != 0) {
//        std::cout << "===[ pid: " << processId << " ]===" << std::endl;
        std::string imageFileName;
        std::vector<std::string> newRegionFiles;
        checkProcess(processId, defaultEnforceType, forceEnforceType, outSubDirPath, imageFileName, newRegionFiles);

//std::cout << "DEBUG: [main] newRegionFiles size: " << newRegionFiles.size() << std::endl;
        if (newRegionFiles.size() > 0) {
            // create config
            auto cfgIter = newRegionConfig.find(imageFileName);
            if (cfgIter == newRegionConfig.end()) {
                auto p = newRegionConfig.insert({imageFileName, std::vector<std::string>()});
                cfgIter = p.first;
            }
            cfgIter->second.insert(cfgIter->second.end(), newRegionFiles.begin(), newRegionFiles.end());
        }
    } else {
        DWORD processIds[NSIZE];
        DWORD bytesNeeded = 0;
        if (!::EnumProcesses(
            processIds,
            NSIZE * sizeof(processIds[0]),
            &bytesNeeded
            )) {
            std::cerr << "ERROR: EnumProcesses failed; error: " << ::GetLastError() << std::endl;
        }

        DWORD countNeeded = bytesNeeded / sizeof(processIds[0]);
        if (countNeeded > NSIZE) {
            std::cerr << "ERROR: EnumProcessModules failed (buffer not big enough -- needed: " << countNeeded << " elems); error: " << ::GetLastError() << std::endl;
            return 1;
        }

        for (size_t i = 0; i < countNeeded; ++i) {
//            std::cout << "===[ pid: " << processIds[i] << " ]===" << std::endl;
            std::string imageFileName;
            std::vector<std::string> newRegionFiles;
            checkProcess(processIds[i], defaultEnforceType, forceEnforceType, outSubDirPath, imageFileName, newRegionFiles);

//std::cout << "DEBUG: [main] newRegionFiles size: " << newRegionFiles.size() << std::endl;
            if (newRegionFiles.size() > 0) {
                // create config
                auto cfgIter = newRegionConfig.find(imageFileName);
                if (cfgIter == newRegionConfig.end()) {
                    auto p = newRegionConfig.insert({imageFileName, std::vector<std::string>()});
                    cfgIter = p.first;
                }
                cfgIter->second.insert(cfgIter->second.end(), newRegionFiles.begin(), newRegionFiles.end());
            }
        }
    }

    // write newConfig under outSubDirPath
    std::string outConfigPath = outSubDirPath;
    if (outConfigPath[outConfigPath.size()-1] != '\\') {
        outConfigPath += '\\';
    }
    outConfigPath += "AppChecker.cfg";
    // NOTE: at this point the parent directory of outConfigPath exists
    writeConfig(newRegionConfig, outConfigPath);

    return 0;
}

void printHelp(std::string cmdName)
{
    std::cout << "Usage: " << cmdName << " [opts]" << std::endl;
    std::cout << "  --configPath str       Path to the configuration file to use.  Default: " << "AppChecker.cfg" << std::endl;
    std::cout << "  --procId int           Process ID to monitor.  If omitted, all processes are monitored" << std::endl;
    std::cout << "  --defaultEnforce int   The default enforcement level.  Default: 0" << std::endl;
    std::cout << "                           0 - no enforcement" << std::endl;
    std::cout << "                           1 - remove exec attribute from suspicious memory" << std::endl;
    std::cout << "                           2 - terminate process" << std::endl;
    std::cout << "  --forceEnforce int     Enforcement level override (see --defaultEnforce for meaning of values).  Default: -1 (no override)" << std::endl;
    std::cout << "  --repeat int           Number of time to repeat scanning.  -1 means infinite repetition.  Default: 1" << std::endl;
}

int main(int argc, const char* argv[])
{
    DWORD dwProcessId = 0;
    std::string configPath = "AppChecker.cfg";
    int defaultEnforceType = 0;  // 0 - no enforcement; 1 - change mem-type; 2 - stop proc
    int forceEnforceType = -1;  // -1 - no override
    int maxRepeatCount = 1;  // -1 means infinite repetition
    int argIdx = 1;
    // TODO: add capability for enforcement
    while (argIdx < argc) {
        std::string optionName = argv[argIdx];
        if (optionName == "--help") {
            printHelp(argv[0]);
            return 0;
        }
        else if (optionName == "--configPath") {
            if (argIdx + 1 >= argc) {
                std::cerr << "ERROR: Missing configPath parameter" << std::endl;
                return 1;
            }

            configPath = argv[argIdx+1];
            argIdx += 2;
        } else if (optionName == "--procId") {
            if (argIdx + 1 >= argc) {
                std::cerr << "ERROR: Missing procId parameter" << std::endl;
                return 1;
            }

            long tmp = std::stol(argv[argIdx+1]);
            if (tmp < 0 || tmp > std::numeric_limits<DWORD>::max()) {
                std::cerr << "ERROR: Invalid processId parameter" << std::endl;
                return 1;
            }

            dwProcessId = static_cast<DWORD>(tmp);
            argIdx += 2;
        } else if (optionName == "--defaultEnforce") {
            if (argIdx + 1 >= argc) {
                std::cerr << "ERROR: Missing defaultEnforce parameter" << std::endl;
                return 1;
            }

            int tmp = std::stoi(argv[argIdx+1]);
            if (tmp < 0 || tmp > 2) {
                std::cerr << "ERROR: Invalid defaultEnforce parameter" << std::endl;
                return 1;
            }

            defaultEnforceType = tmp;
            argIdx += 2;
        } else if (optionName == "--forceEnforce") {
            if (argIdx + 1 >= argc) {
                std::cerr << "ERROR: Missing forceEnforce parameter" << std::endl;
                return 1;
            }

            int tmp = std::stoi(argv[argIdx+1]);
            if (tmp < 0 || tmp > 2) {
                std::cerr << "ERROR: Invalid forceEnforce parameter" << std::endl;
                return 1;
            }

            forceEnforceType = tmp;
            argIdx += 2;
        } else if (optionName == "--repeat") {
            if (argIdx + 1 >= argc) {
                std::cerr << "ERROR: Missing repeat parameter" << std::endl;
                return 1;
            }

            int tmp = std::stoi(argv[argIdx+1]);
            if ((tmp < -1) || (tmp == 0)) {
                std::cerr << "ERROR: Invalid repeat parameter" << std::endl;
                return 1;
            }

            maxRepeatCount = tmp;
            argIdx += 2;
        } else {
            std::cerr << "ERROR: Uknown parameter: " << optionName << std::endl;
            return 1;
        }
    }

    readConfig(configPath, knownRegions, outDirPath);

    // check existence of out dir and create it if needed
    std::filesystem::create_directories(outDirPath);

    // TODO: read in region files from out dir (descend into subdirs) into newRegions
    for (const auto& fsEntry : std::filesystem::directory_iterator(outDirPath)) {
        std::error_code ec;
        if (fsEntry.is_directory(ec)) {
//std::cout << "DEBUG: [main] fsEntry: " << fsEntry.path() << std::endl; 
            std::string newConfigPath = fsEntry.path().string() + "/AppChecker.cfg";
            std::string ignore;
//std::cout << "DEBUG: [main] newConfigPath: " << newConfigPath << std::endl;
//std::cout.flush();
            readConfig(newConfigPath, newRegions, ignore);
        }
    }

    bool stop = false;
    int currRepeatCount = 0;
    while(!stop) {
        if ((maxRepeatCount > 0) && (currRepeatCount >= maxRepeatCount)) {
            break;
        }
        ++currRepeatCount;

        performChecks(dwProcessId, defaultEnforceType, forceEnforceType, outDirPath);
    }

//    printf("Done\n");

    return 0;
}
