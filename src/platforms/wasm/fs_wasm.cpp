
#ifdef __EMSCRIPTEN__


#include <emscripten.h>
#include <emscripten/emscripten.h> // Include Emscripten headers
#include <emscripten/html5.h>
#include <emscripten/bind.h>
#include <emscripten/val.h>


#include <map>
#include <mutex>
#include <vector>
#include <stdio.h>


#include "fl/file_system.h"
#include "fl/math_macros.h"
#include "namespace.h"
#include "fl/ptr.h"
#include "fl/str.h"
#include "fl/json.h"
#include "fl/warn.h"

using namespace fl;

FASTLED_NAMESPACE_BEGIN

FASTLED_SMART_PTR(FsImplWasm);
FASTLED_SMART_PTR(WasmFileHandle);

// Map is great because it doesn't invalidate it's data members unless erase is
// called.
FASTLED_SMART_PTR(FileData);

class FileData : public fl::Referent {
  public:

    FileData(size_t capacity) : mCapacity(capacity) {}
    FileData(const std::vector<uint8_t> &data, size_t len)
        : mData(data), mCapacity(len) {}
    FileData() = default;

    void append(const uint8_t *data, size_t len) {
        std::lock_guard<std::mutex> lock(mMutex);
        mData.insert(mData.end(), data, data + len);
        mCapacity = MAX(mCapacity, mData.size());
    }

    size_t read(size_t pos, uint8_t *dst, size_t len) {
        std::lock_guard<std::mutex> lock(mMutex);
        if (pos >= mData.size()) {
            return 0;
        }
        size_t bytesAvailable = mData.size() - pos;
        size_t bytesToActuallyRead = MIN(len, bytesAvailable);
        auto begin_it = mData.begin() + pos;
        auto end_it = begin_it + bytesToActuallyRead;
        std::copy(begin_it, end_it, dst);
        return bytesToActuallyRead;
    }

    size_t bytesRead() const {
        std::lock_guard<std::mutex> lock(mMutex);
        return mData.size();
    }

    size_t capacity() const {
        std::lock_guard<std::mutex> lock(mMutex);
        return mCapacity;
    }
private:
    std::vector<uint8_t> mData;
    size_t mCapacity = 0;
    mutable std::mutex mMutex;
};

typedef std::map<Str, FileDataPtr> FileMap;
static FileMap gFileMap;
// At the time of creation, it's unclear whether this can be called by multiple
// threads. With an std::map items remain valid while not erased. So we only
// need to protect the map itself for thread safety. The values in the map are
// safe to access without a lock.
static std::mutex gFileMapMutex;


class WasmFileHandle : public fl::FileHandle {
  private:
    FileDataPtr mData;
    size_t mPos;
    Str mPath;

  public:
    WasmFileHandle(const Str &path, const FileDataPtr data)
        : mPath(path), mData(data), mPos(0) {}

    virtual ~WasmFileHandle() override {}

    bool available() const override {
        if (mPos >= mData->capacity()) {
            return false;
        }
        if (mData->bytesRead() > mPos) {
            return true;
        }
        return false;
    }
    size_t bytesLeft() const override {
        if (!available()) {
            return 0;
        }
        return mData->capacity() - mPos;
    }
    size_t size() const override { return mData->capacity(); }

    size_t read(uint8_t *dst, size_t bytesToRead) override {
        size_t bytesRead = mData->read(mPos, dst, bytesToRead);
        mPos += bytesRead;
        return bytesRead;
    }

    size_t pos() const override { return mPos; }
    const char *path() const override { return mPath.c_str(); }

    bool seek(size_t pos) override {
        if (pos > mData->capacity()) {
            return false;
        }
        mPos = pos;
        return true;
    }

    void close() override {
        // No need to do anything for in-memory files
    }

    bool valid() const override { return true; }  // always valid if we can open a file.
};

class FsImplWasm : public fl::FsImpl {
  public:
    FsImplWasm() = default;
    ~FsImplWasm() override {}

    bool begin() override { return true; }
    void end() override {}

    void close(FileHandlePtr file) override {
        printf("Closing file %s\n", file->path());
        if (file) {
            file->close();
        }
    }

    fl::FileHandlePtr openRead(const char *_path) override {
        printf("Opening file %s\n", _path);
        Str path(_path);
        std::lock_guard<std::mutex> lock(gFileMapMutex);
        auto it = gFileMap.find(path);
        if (it != gFileMap.end()) {
            auto &data = it->second;
            WasmFileHandlePtr out =
                WasmFileHandlePtr::TakeOwnership(new WasmFileHandle(path, data));
            return out;
        }
        return fl::FileHandlePtr::Null();
    }
};



FileDataPtr _findIfExists(const Str& path) {
    std::lock_guard<std::mutex> lock(gFileMapMutex);
    auto it = gFileMap.find(path);
    if (it != gFileMap.end()) {
        return it->second;
    }
    return FileDataPtr::Null();
}

FileDataPtr _findOrCreate(const Str& path, size_t len) {
    std::lock_guard<std::mutex> lock(gFileMapMutex);
    auto it = gFileMap.find(path);
    if (it != gFileMap.end()) {
        return it->second;
    }
    auto entry = FileDataPtr::New(len);
    gFileMap.insert(std::make_pair(path, entry));
    return entry;
}

FileDataPtr _createIfNotExists(const Str& path, size_t len) {
    std::lock_guard<std::mutex> lock(gFileMapMutex);
    auto it = gFileMap.find(path);
    if (it != gFileMap.end()) {
        return FileDataPtr::Null();
    }
    auto entry = FileDataPtr::New(len);
    gFileMap.insert(std::make_pair(path, entry));
    return entry;
}

FASTLED_NAMESPACE_END


FASTLED_USING_NAMESPACE



extern "C" {


EMSCRIPTEN_KEEPALIVE bool jsInjectFile(const char *path, const uint8_t *data,
                                       size_t len) {

    auto inserted = _createIfNotExists(Str(path), len);
    if (!inserted) {
        FASTLED_WARN("File can only be injected once.");
        return false;
    }
    inserted->append(data, len);
    return true;
}

EMSCRIPTEN_KEEPALIVE bool jsAppendFile(const char *path, const uint8_t *data,
                                       size_t len) {
    printf("Appending file %s with %lu bytes\n", path, len);
    auto entry = _findIfExists(Str(path));
    if (!entry) {
        FASTLED_WARN("File must be declared before it can be appended.");
        return false;
    }
    entry->append(data, len);
    return true;
}

EMSCRIPTEN_KEEPALIVE bool jsDeclareFile(const char *path, size_t len) {
    // declare a file and it's length. But don't fill it in yet
    auto inserted = _createIfNotExists(Str(path), len);
    if (!inserted) {
        FASTLED_WARN("File can only be declared once.");
        return false;
    }
    return true;
}


EMSCRIPTEN_KEEPALIVE void fastled_declare_files(std::string jsonStr) {
    fl::JsonDocument doc;
    fl::parseJson(jsonStr.c_str(), &doc);
    auto files = doc["files"];
    if (files.isNull()) {
        return;
    }
    auto files_array = files.as<FLArduinoJson::JsonArray>();
    if (files_array.isNull()) {
        return;
    }

    for (auto file : files_array) {
        auto size_obj = file["size"];
        if (size_obj.isNull()) {
            continue;
        }
        auto size = size_obj.as<int>();
        auto path_obj = file["path"];
        if (path_obj.isNull()) {
            continue;
        }
        printf("Declaring file %s with size %d. These will become available as File system paths within the app.\n", path_obj.as<const char*>(), size);
        jsDeclareFile(path_obj.as<const char*>(), size);
    }
}



} // extern "C"




EMSCRIPTEN_BINDINGS(_fastled_declare_files) {
    emscripten::function("_fastled_declare_files", &fastled_declare_files);
    //emscripten::function("jsAppendFile", emscripten::select_overload<bool(const char*, const uint8_t*, size_t)>(&jsAppendFile), emscripten::allow_raw_pointer<arg<0>, arg<1>>());
};

namespace fl {
// Platforms eed to implement this to create an instance of the filesystem.
FsImplPtr make_sdcard_filesystem(int cs_pin) {
    return FsImplWasmPtr::New();
}
}




#endif // __EMSCRIPTEN__
