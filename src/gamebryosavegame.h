#pragma once

#include <string>
#include <fstream>
#include <vector>
#include <memory>
#include <napi.h>
#include "fmt/format.h"

#include "string_cast.h"

class DataInvalid : public std::runtime_error {
public:
  DataInvalid(const char* message, size_t offset) : std::runtime_error(message), m_Offset(offset) {}
  size_t offset() const { return m_Offset; }
private:
  size_t m_Offset;
};

class IDecoder {
public:
  virtual ~IDecoder() {};
  virtual bool seek(size_t offset, std::ios_base::seekdir dir = std::ios::beg) = 0;
  virtual size_t tell() = 0;
  virtual bool read(char *buffer, size_t size) = 0;
  virtual void clear() = 0;
};

/**
 * Stores a screenshot in 32-bit rgba format
 * (storing an alpha channels seems pointless but the javascript side probably needs it
 *  and fallout4 contains an alpha channel anyway so this minimizes conversion steps. probably)
 */
class Dimensions {
public:
  Dimensions()
    : m_Width(0), m_Height(0) {}

  Dimensions(uint32_t width, uint32_t height)
    : m_Width(width), m_Height(height) {}

  uint32_t width() const { return m_Width; }
  uint32_t height() const { return m_Height; }

private:
  uint32_t m_Width;
  uint32_t m_Height;
};

Napi::Value create(const Napi::CallbackInfo &info);

class GamebryoSaveGame : public Napi::ObjectWrap<GamebryoSaveGame>
{
public:

  static Napi::Object Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "GamebryoSaveGame", {
      InstanceAccessor("characterName", &GamebryoSaveGame::characterName, nullptr, napi_enumerable),
      InstanceAccessor("characterLevel", &GamebryoSaveGame::characterLevel, nullptr, napi_enumerable),
      InstanceAccessor("location", &GamebryoSaveGame::location, nullptr, napi_enumerable),
      InstanceAccessor("saveNumber", &GamebryoSaveGame::saveNumber, nullptr, napi_enumerable),
      InstanceAccessor("plugins", &GamebryoSaveGame::plugins, nullptr, napi_enumerable),
      InstanceAccessor("creationTime", &GamebryoSaveGame::creationTime, nullptr, napi_enumerable),
      InstanceAccessor("fileName", &GamebryoSaveGame::fileName, nullptr, napi_enumerable),
      InstanceAccessor("screenshotSize", &GamebryoSaveGame::screenshotSize, nullptr, napi_enumerable),
      InstanceAccessor("playTime", &GamebryoSaveGame::playTime, nullptr, napi_enumerable),
      InstanceMethod("getScreenshot", &GamebryoSaveGame::getScreenshot),
      });
    Napi::FunctionReference* constructor = new Napi::FunctionReference();
    *constructor = Napi::Persistent(func);
    exports.Set("GamebryoSaveGame", func);

    env.SetInstanceData<Napi::FunctionReference>(constructor);
    return exports;
  }

  GamebryoSaveGame(const Napi::CallbackInfo &info);

  static Napi::Object CreateNewItem(Napi::Env env) {
    Napi::FunctionReference* constructor = env.GetInstanceData<Napi::FunctionReference>();
    // this creates the object with no filename set so it doesn't get read at this point, allowing it to be read
    // asynchronously later
    return constructor->New({ env.Null() });
  }

  virtual ~GamebryoSaveGame();

  void readAsync(const Napi::Env& env, const std::string& fileName, bool quick, const Napi::Function& cb);

  // creation time in seconds since the unix epoch
  Napi::Value creationTime(const Napi::CallbackInfo &info) { return Napi::Number::New(info.Env(), m_CreationTime); }
  Napi::Value characterName(const Napi::CallbackInfo &info) { return Napi::String::New(info.Env(), m_PCName); }
  Napi::Value characterLevel(const Napi::CallbackInfo &info) { return Napi::Number::New(info.Env(), m_PCLevel); }
  Napi::Value location(const Napi::CallbackInfo &info) { return Napi::String::New(info.Env(), m_PCLocation); }
  Napi::Value saveNumber(const Napi::CallbackInfo &info) { return Napi::Number::New(info.Env(), m_SaveNumber); }
  Napi::Value plugins(const Napi::CallbackInfo& info) {
    Napi::Array res = Napi::Array::New(info.Env());
    int idx = 0;
    for (const std::string& plugin : m_Plugins) {
      res.Set(idx++, Napi::String::New(info.Env(), plugin));
    }
    return res;
  }
  Napi::Value screenshotSize(const Napi::CallbackInfo &info) {
    Napi::Object result = Napi::Object::New(info.Env());
    result.Set("width",  Napi::Number::New(info.Env(), m_ScreenshotDim.width()));
    result.Set("height", Napi::Number::New(info.Env(), m_ScreenshotDim.height()));
    return result;
  }
  Napi::Value playTime(const Napi::CallbackInfo &info) { return Napi::String::New(info.Env(), m_Playtime); }
  
  Napi::Value getScreenshot(const Napi::CallbackInfo &info) {
    Napi::Buffer<uint8_t> buffer = Napi::Buffer<uint8_t>::New(info.Env(), m_Screenshot.size());

    uint8_t *outData = buffer.Data();
    memcpy(outData, m_Screenshot.data(), (std::min)(buffer.ByteLength(), m_Screenshot.size()));
    return buffer;
  }

  const std::vector<uint8_t> &screenshotData() const {
    return m_Screenshot;
  }

  Napi::Value fileName(const Napi::CallbackInfo &info) { return Napi::String::New(info.Env(), m_FileName); }

private:

  friend class FileWrapper;

  class FileWrapper
  {
  public:
    /** Construct the save file information.
     * @params expected - expect bytes at start of file
     **/
    FileWrapper(GamebryoSaveGame *game, CodePage encoding);

    /** Set this for save games that have a marker at the end of each
     * field. Specifically fallout
     **/
    void setHasFieldMarkers(bool);

    /** Set bz string mode (1 byte length, null terminated)
     **/
    void setBZString(bool);

    bool header(const char *expected);

    template <typename T> void skip(int count = 1)
    {
      if (!m_Decoder->seek(count * sizeof(T), std::ios::cur)) {
        m_Decoder->clear();
        m_Decoder->seek(0, std::ios::end);
        throw std::runtime_error(fmt::format("unexpected end of file at \"{}\" (skip of \"{}\" bytes)", m_Decoder->tell(), count * sizeof(T)).c_str());
      }
    }

    template <typename T> void read(T &value)
    {
      if (!m_Decoder->read(reinterpret_cast<char*>(&value), sizeof(T))) {
        m_Decoder->clear();
        m_Decoder->seek(0, std::ios::end);
        throw std::runtime_error(fmt::format("unexpected end of file at \"{}\" (read of \"{}\" bytes)", m_Decoder->tell(), sizeof(T)).c_str());
      }
      if (m_HasFieldMarkers) {
        char marker;
        m_Decoder->read(&marker, 1);
        sanityCheck(marker == '|', "Expected field separator");
      }
    }

    void readBString(std::string &value);

    uint64_t tell()
    {
      return m_Decoder->tell();
    }

    void seek(uint64_t pos) {
      m_Decoder->seek(pos);
    }

    void read(void *buff, std::size_t length);

    /* Reads RGB image from save
     * Assumes picture dimensions come immediately before the save
     */
    void readImage(bool alpha = false);

    /* Reads RGB image from save */
    void readImage(unsigned long width, unsigned long height, bool alpha = false);

    /* Read the plugin list */
    void readPlugins(bool bStrings = false);

    /* Read the list of light plugins */
    void readLightPlugins();

    /* treat the following bytes as compressed */
    void setCompression(unsigned short format, unsigned long compressedSize, unsigned long uncompressedSize);

    void sanityCheck(bool conditionMatch, const char* message);

  private:
    GamebryoSaveGame *m_Game;
    std::shared_ptr<IDecoder> m_Decoder;
    bool m_HasFieldMarkers;
    bool m_BZString;
    CodePage m_Encoding;
  };

  CodePage determineEncoding(const std::string &fileName);

  void read();
  void readOblivion(FileWrapper &file);
  void readSkyrim(FileWrapper &file);
  void readFO3(FileWrapper &file);
  void readFO4(FileWrapper &file);

private:

  Napi::ThreadSafeFunction m_ThreadCB;

  bool m_QuickRead;
  std::string m_FileName;
  std::string m_PCName;
  uint16_t m_PCLevel;
  std::string m_PCLocation;
  std::string m_Playtime;
  uint32_t m_SaveNumber;
  uint32_t m_CreationTime;
  std::vector<std::string> m_Plugins;
  Dimensions m_ScreenshotDim;
  std::vector<uint8_t> m_Screenshot;

};

template <> void GamebryoSaveGame::FileWrapper::read<std::string>(std::string &);


Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
  GamebryoSaveGame::Init(env, exports);

  exports.Set("create", Napi::Function::New(env, create));

  return exports;
}

NODE_API_MODULE(GamebryoSaveGame, InitAll)


/*
NBIND_GLOBAL() {
  function(create);
}
*/
