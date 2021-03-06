//
// Created by Haifa Bogdan Adnan on 03/08/2018.
//

#if defined(__x86_64__) || defined(__i386__)
    #include <cpuinfo_x86.h>
#endif
#if defined(__arm__)
    #include <cpuinfo_arm.h>
#endif


#include "../../common/common.h"
#include "../../app/arguments.h"

#include "../hasher.h"
#include "../argon2/argon2.h"

#include "cpu_hasher.h"

#include <dlfcn.h>

cpu_hasher::cpu_hasher() : hasher() {
    _type = "CPU";
    __optimization = "REF";
    __available_processing_thr = 1;
    __available_memory_thr = 1;
    __threads_count = 0;
    __running = false;
    __argon2_blocks_filler_ptr = NULL;
    _description = __detect_features_and_make_description();
}

cpu_hasher::~cpu_hasher() {
    __running = false;
    for(vector<thread*>::iterator it = __runners.begin();it != __runners.end();++it) {
        (*it)->join();
        delete *it;
    }
}

bool cpu_hasher::configure(int intensity) {
    __load_argon2_block_filler();

    if(__argon2_blocks_filler_ptr == NULL) {
        _intensity = 0;
        _description += "Status: DISABLED - argon2 hashing module not found.";
        return false;
    }

    __threads_count = min(__available_processing_thr, __available_memory_thr);

    if (__threads_count == 0) {
        _intensity = 0;
        _description += "Status: DISABLED - not enough resources.";
        return false;
    }

    if (intensity == 0) {
        _intensity = 0;
        _description += "Status: DISABLED - by user.";
        return false;
    }

    _intensity = intensity;

    __threads_count = __threads_count * _intensity / 100;
    if (__threads_count == 0)
        __threads_count = 1;

    __running = true;
    for(int i=0;i<__threads_count;i++) {
        __runners.push_back(new thread(&cpu_hasher::__run, ref(*this)));
    }

    _description += "Status: ENABLED - with " + to_string(__threads_count) + " threads.";

    return true;
}

string cpu_hasher::__detect_features_and_make_description() {
    stringstream ss;
#if defined(__x86_64__) || defined(__i386__)
    char brand_string[49];
    cpu_features::FillX86BrandString(brand_string);

    ss << brand_string << endl;

    cpu_features::X86Features features = cpu_features::GetX86Info().features;
    ss << "Optimization features: ";

#if defined(__x86_64__)
    ss << "SSE2 ";
    __optimization = "SSE2";
#else
    __optimization = "REF";
#endif

    if(features.ssse3 || features.avx2 || features.avx512f) {
        if (features.ssse3) {
            ss << "SSSE3 ";
            __optimization = "SSSE3";
        }
        if (features.avx2) {
            ss << "AVX2 ";
            __optimization = "AVX2";
        }
        if (features.avx512f) {
            ss << "AVX512F ";
            __optimization = "AVX512F";
        }
    }
    else {
        ss << "none";
    }
    ss << endl;

    ss << "Will use " << __optimization << " for hashing algorithm." << endl;
#endif
#if defined(__arm__)
    ss << "ARM processor" << endl;
    ss << "Optimization features: none" << endl;

    __optimization = "REF";
    ss << "Will use " << __optimization << " for hashing algorithm." << endl;
#endif

    __available_processing_thr = thread::hardware_concurrency();
    ss << "Parallelism: " << __available_processing_thr << " concurent threads supported." << endl;

    //check available memory
    vector<void *> memory_test;
    for(__available_memory_thr = 0;__available_memory_thr < __available_processing_thr;__available_memory_thr++) {
        void *memory = malloc(ARGON2_MEMORY_SIZE + 32); //32 bytes for alignament
        if(memory == NULL)
            break;
        memory_test.push_back(memory);
    }
    for(vector<void*>::iterator it=memory_test.begin();it != memory_test.end();++it) {
        free(*it);
    }
    ss << "Memory: there is enough memory for " << __available_memory_thr << " concurent threads." << endl;

    return ss.str();
}

void cpu_hasher::__run() {
    void *memory = malloc(ARGON2_MEMORY_SIZE + 256);
    size_t allocated_size = ARGON2_MEMORY_SIZE + 256;
    uint8_t* aligned = (uint8_t*)align(32, ARGON2_MEMORY_SIZE, memory, allocated_size);

    argon2 hash_factory(__argon2_blocks_filler_ptr, 1, aligned, ARGON2_MEMORY_SIZE, NULL);

    while(__running) {
        string base = get_base();
        if(!base.empty()) {
            vector<string> hashes = hash_factory.generate_hashes(base);
            for(auto it = hashes.begin(); it != hashes.end(); ++it) {
                _store_hash(*it);
            }
        }
    }

    free(memory);
}

void cpu_hasher::__load_argon2_block_filler() {
    string module_path = arguments::get_app_folder();
    module_path += "/modules/argon2_fill_blocks_" + __optimization + ".opt";
    void *handle = dlopen(module_path.c_str(), RTLD_LAZY);
    if(handle != NULL)
        __argon2_blocks_filler_ptr = (argon2_blocks_filler_ptr)dlsym(handle, "fill_memory_blocks");
}

REGISTER_HASHER(cpu_hasher);