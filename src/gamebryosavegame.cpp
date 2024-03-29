#include "gamebryosavegame.h"

#include <sys/stat.h>
#include <stdexcept>
#include <vector>
#include <ctime>
#include <sstream>
#include <lz4.h>
#include <zlib.h>
#include <iostream>
#include <thread>
#include <fstream>

uint32_t windowsTicksToEpoch(int64_t windowsTicks)
{
  // windows tick is in 100ns
  static const int64_t WINDOWS_TICK = 10000000;
  // windows epoch is 1601-01-01T00:00:00Z which is this many seconds before the unix epoch
  static const int64_t SEC_TO_UNIX_EPOCH = 11644473600LL;

  return static_cast<uint32_t>(windowsTicks / WINDOWS_TICK - SEC_TO_UNIX_EPOCH);
}

class MoreInfoException : public std::exception {
public:
  MoreInfoException(const char *message, const char *syscall, const std::string &fileName, int code)
    : std::exception(std::runtime_error(message))
    , m_SysCall(syscall)
    , m_FileName(fileName)
    , m_ErrorCode(code)
  {}

private:
  std::string m_SysCall;
  std::string m_FileName;
  int m_ErrorCode;

public:
  std::string syscall() const { return m_SysCall; }
  std::string fileName() const { return m_FileName; }
  int errorCode() const { return m_ErrorCode; }
};

class DirectDecoder : public IDecoder {
public:
  DirectDecoder(const std::string &fileName)
    : m_File(toWC(fileName.c_str(), CodePage::UTF8, fileName.length()).c_str(), std::ios::in | std::ios::binary)
  {
    if (!m_File.is_open()) {
      throw MoreInfoException(strerror(errno), "open", fileName, errno);
    }
  }

  virtual size_t tell() {
    return m_File.tellg();
  }

  virtual bool seek(size_t offset, std::ios_base::seekdir dir = std::ios::beg) {
    return static_cast<bool>(m_File.seekg(offset, dir));
  }

  virtual bool read(char *buffer, size_t size) {
    return static_cast<bool>(m_File.read(buffer, size));
  }

  virtual void clear() {
    m_File.clear();
  }
private:
  std::ifstream m_File;
};

class LZ4Decoder : public IDecoder {
public:
  LZ4Decoder(std::shared_ptr<IDecoder> &wrapee, unsigned long compressedSize, unsigned long uncompressedSize)
  {
    std::string tempCompressed;
    tempCompressed.resize(compressedSize);
    std::string tempUncompressed;
    tempUncompressed.resize(uncompressedSize);

    wrapee->read(&tempCompressed[0], compressedSize);
    LZ4_decompress_safe(&tempCompressed[0], &tempUncompressed[0], compressedSize, uncompressedSize);
    m_Buffer = std::istringstream(tempUncompressed);
  }

  virtual size_t tell() {
    return m_Buffer.tellg();
  }

  virtual bool seek(size_t offset, std::ios_base::seekdir dir = std::ios::beg) {
    return static_cast<bool>(m_Buffer.seekg(offset, dir));
  }

  virtual bool read(char *buffer, size_t size) {
    return static_cast<bool>(m_Buffer.read(buffer, size));
  }

  virtual void clear() {
    m_Buffer.clear();
  }
private:
  std::istringstream m_Buffer;
};

class ZlibDecoder : public IDecoder {
public:
  ZlibDecoder(std::shared_ptr<IDecoder> &wrapee, unsigned long compressedSize, unsigned long uncompressedSize)
  {
    std::string tempCompressed;
    tempCompressed.resize(compressedSize);

    std::string tempUncompressed;
    tempUncompressed.resize(uncompressedSize);

    wrapee->read(&tempCompressed[0], compressedSize);

    z_stream infstream;
    infstream.zalloc = Z_NULL;
    infstream.zfree = Z_NULL;
    infstream.opaque = Z_NULL;
    infstream.avail_in = compressedSize;
    infstream.next_in = reinterpret_cast<Bytef*>(&tempCompressed[0]);
    infstream.avail_out = uncompressedSize;
    infstream.next_out = reinterpret_cast<Bytef*>(&tempUncompressed[0]);

    int res = inflateInit(&infstream);
    if (res != Z_OK) {
      throw std::runtime_error("failed to initialize zlib inflate");
    }
    inflate(&infstream, Z_NO_FLUSH);
    inflateEnd(&infstream);

    m_Buffer = std::istringstream(tempUncompressed);
  }

  virtual size_t tell() {
    return m_Buffer.tellg();
  }

  virtual bool seek(size_t offset, std::ios_base::seekdir dir = std::ios::beg) {
    return static_cast<bool>(m_Buffer.seekg(offset, dir));
  }

  virtual bool read(char *buffer, size_t size) {
    return static_cast<bool>(m_Buffer.read(buffer, size));
  }

  virtual void clear() {
    m_Buffer.clear();
  }
private:
  std::istringstream m_Buffer;
};

bool isCharInRange(wchar_t ch, wchar_t low, wchar_t high) {
  return ch >= low && ch <= high;
}

bool isCharCyrillic(wchar_t ch) {
  // this doesn't cover all cyrillic characters in Unicode, only standard characters and "supplement", the rest is all over the place
  return isCharInRange(ch, 0x400, 0x52F);
}

bool ignoreChar(wchar_t ch) {
  return isCharInRange(ch, L'0', L'9') || (ch == L'-') || (ch == L'.') || (ch == L' ');
}

void GamebryoSaveGame::readAsync(const Napi::Env &env, const std::string &fileName, bool quick, const Napi::Function& cb) {
  m_FileName = fileName;
  m_QuickRead = quick;

  std::thread* loadThread;

  m_ThreadCB = Napi::ThreadSafeFunction::New(env, cb,
    "AsyncLoadCB", 0, 1, [loadThread](Napi::Env) {
    loadThread->join();
    delete loadThread;
  });

  loadThread = new std::thread{ [this, env]() {
    auto callback = [](Napi::Env env, Napi::Function jsCallback, GamebryoSaveGame* result) {
      jsCallback.Call({ env.Null(), result->Value() });
    };

    auto callbackError = [](Napi::Env env, Napi::Function jsCallback, std::string* err = nullptr) {
      Napi::Error errRef = Napi::Error::New(env, err->c_str());
      jsCallback.Call({ static_cast<napi_value>(errRef.Value()) });
    };

    try {
      read();
      m_ThreadCB.Acquire();
      m_ThreadCB.BlockingCall(this, callback);
    }
    catch (const std::exception& e) {
      std::string* err = new std::string{ e.what() };
      m_ThreadCB.Acquire();
      m_ThreadCB.BlockingCall(err, callbackError);
    }

    m_ThreadCB.Release();
  } };
}

GamebryoSaveGame::GamebryoSaveGame(const Napi::CallbackInfo &info)
  : Napi::ObjectWrap<GamebryoSaveGame>(info)
  , m_PCLevel(0)
  , m_SaveNumber()
  , m_CreationTime(0)
{
  Ref();

  if ((info.Length() == 1) && (info[0] == info.Env().Null())) {
    // allow reading asynchronously later
  } else {
    try {
      m_FileName = info[0].ToString();
      m_QuickRead = info[1].ToBoolean();
      read();
    }
    catch (const std::exception& e) {
      throw Napi::Error::New(info.Env(), e.what());
    }
  }
}

void GamebryoSaveGame::read() {
  CodePage encoding = determineEncoding(m_FileName);
  {
    FileWrapper file(this, encoding);

    bool found = false;
    for (auto hdr : {
      std::make_pair("TES4SAVEGAME", &GamebryoSaveGame::readOblivion),
      std::make_pair("TESV_SAVEGAME", &GamebryoSaveGame::readSkyrim),
      std::make_pair("FO3SAVEGAME", &GamebryoSaveGame::readFO3),
      std::make_pair("FO4_SAVEGAME", &GamebryoSaveGame::readFO4)
    }) {
      if (file.header(hdr.first)) {
        found = true;
        (this->*hdr.second)(file);
      }
    }

    if (!found) {
      throw std::runtime_error("invalid file header");
    }
  }

  if (m_CreationTime == 0) {
#ifdef _WIN32
    struct _stat fileStat;
    int res = _wstat(toWC(m_FileName.c_str(), CodePage::UTF8, m_FileName.size()).c_str(), &fileStat);
#else
    struct stat fileStat;
    int res = stat(m_FileName.c_str(), &fileStat);
#endif
    if (res == 0) {
      m_CreationTime = static_cast<uint32_t>(fileStat.st_mtime);
    }
  }
}

GamebryoSaveGame::~GamebryoSaveGame()
{
  Unref();
}

// don't want no dependency on windows header
struct WINSYSTEMTIME {
  uint16_t wYear;
  uint16_t wMonth;
  uint16_t wDayOfWeek;
  uint16_t wDay;
  uint16_t wHour;
  uint16_t wMinute;
  uint16_t wSecond;
  uint16_t wMilliseconds;
};

CodePage GamebryoSaveGame::determineEncoding(const std::string &fileName) {
  // determine code page based on file-name. Currently only supporting cyrillic
  // This is a heuristic: if more than 50% of the file name (which is unicode) are cyrillic characters,
  // we assume the file _content_ (which is single-byte characters) is in the corresponding code page
#ifdef _WIN32
  // pretty hacky way to reduce the file path to just the name without extension
  size_t nameOffset = fileName.find_last_of("/\\");
  nameOffset = nameOffset == std::string::npos ? 0 : nameOffset + 1;
  std::wstring fileNameW = toWC(fileName.c_str() + nameOffset, CodePage::UTF8, fileName.size() - nameOffset - 4);

  // filter out numbers and symbols that are identical across code pages anyway
  std::wstring relevantChars;
  std::copy_if(fileNameW.begin(), fileNameW.end(), std::back_inserter(relevantChars), [](wchar_t ch) { return !ignoreChar(ch); });

  // determine percentage of cyrillic characters, if > 50%, assume the file is encoded cyrillic
  int cyrillicChars = std::count_if(relevantChars.begin(), relevantChars.end(),
    [](wchar_t ch) { return isCharCyrillic(ch); });

  if ((relevantChars.length() > 0)
      && ((cyrillicChars * 100) / relevantChars.length() > 50)) {
    return CodePage::CYRILLIC;
  }
#endif
  // the chinese version at least seems to use unicode.
  // Just in case decoding as utf8 doesn't work, assume it's latin1
  return CodePage::UTF8ORLATIN1;
}

void GamebryoSaveGame::readOblivion(GamebryoSaveGame::FileWrapper &file)
{
  file.setBZString(true);

  file.skip<unsigned char>(); //Major version
  file.skip<unsigned char>(); //Minor version

  file.skip<WINSYSTEMTIME>();  // exe last modified (!)

  file.skip<unsigned long>(); //Header version
  file.skip<unsigned long>(); //Header size

  file.read(m_SaveNumber);

  file.read(m_PCName);
  file.read(m_PCLevel);
  file.read(m_PCLocation);

  float gameDays;
  file.read(gameDays); //game days
  m_Playtime =
    std::to_string(static_cast<int>(floor(gameDays))) + " days, "
    + std::to_string(static_cast<int>(static_cast<int>(gameDays * 24) % 24)) + " hours";
  file.skip<unsigned long>(); //game ticks

  WINSYSTEMTIME winTime;
  file.read(winTime);
  tm timeStruct;
  timeStruct.tm_year = winTime.wYear - 1900;
  timeStruct.tm_mon = winTime.wMonth - 1;
  timeStruct.tm_mday = winTime.wDay;
  timeStruct.tm_hour = winTime.wHour;
  timeStruct.tm_min = winTime.wMinute;
  timeStruct.tm_sec = winTime.wSecond; 
  m_CreationTime = mktime(&timeStruct);

  if (!m_QuickRead) {
    //Note that screenshot size, width, height and data are apparently the same
    //structure
    file.skip<unsigned long>(); //Screenshot size.

    file.readImage();

    file.readPlugins(true);
  }
}

void GamebryoSaveGame::readSkyrim(GamebryoSaveGame::FileWrapper &file)
{
  file.skip<unsigned long>(); // header size
  unsigned long version;
  file.read(version); // header version
  file.read(m_SaveNumber);

  file.read(m_PCName);

  unsigned long temp;
  file.read(temp);
  m_PCLevel = static_cast<unsigned short>(temp);

  file.read(m_PCLocation);
  file.read(m_Playtime);

  std::string race;
  file.read(race); // race name (i.e. BretonRace)

  file.skip<unsigned short>(); // Player gender (0 = male)
  file.skip<float>(2); // experience gathered, experience required

  uint64_t ftime;
  file.read(ftime);
  m_CreationTime = windowsTicksToEpoch(ftime);

  if (!m_QuickRead) {
    if (version < 0x0c) {
      // original skyrim format
      file.readImage();
    }
    else {
      // Skyrim SE - same header, different version
      unsigned long width;
      file.read(width);
      unsigned long height;
      file.read(height);
      unsigned short compressionFormat;
      file.read(compressionFormat);

      file.readImage(width, height, true);

      // the rest of the file is compressed in Skyrim SE
      unsigned long compressed, uncompressed;
      file.read(uncompressed);
      file.read(compressed);

      file.setCompression(compressionFormat, compressed, uncompressed);
    }

    unsigned char formVersion;
    file.read(formVersion); // form version
    file.skip<unsigned long>(); // plugin info size
    file.readPlugins();

    if (formVersion >= 0x4e) {
      file.readLightPlugins();
    }
  }
}

void GamebryoSaveGame::readFO3(GamebryoSaveGame::FileWrapper &file)
{
  file.skip<unsigned long>(); //Save header size

  file.skip<unsigned long>(); //File version? always 0x30
  file.skip<unsigned char>(); //Delimiter

  // New Vegas has the same extension, file header, and version (if the previous field was, in fact,
  // a version field), but it has a string field here which FO3 doesn't have 

  uint64_t pos = file.tell();
  int fieldSize = 0;
  for (unsigned char ignore = 0; ignore != 0x7c; ++fieldSize) {
    file.read(ignore); // unknown
  }

  if (fieldSize == 5) {
    // if the field was only 4 bytes, it was a FO3 save after all so seek back since we need the
    // content of that field
    file.seek(pos);
  }

  file.setHasFieldMarkers(true);

  unsigned long width;
  file.read(width);

  unsigned long height;
  file.read(height);

  file.read(m_SaveNumber);

  file.read(m_PCName);

  std::string unknown;
  file.read(unknown);

  long level;
  file.read(level);
  m_PCLevel = level;

  file.read(m_PCLocation);

  file.read(m_Playtime);

  if (!m_QuickRead) {
    file.readImage(width, height);

    file.skip<char>(5); // unknown byte, size of plugin data

    file.readPlugins();
  }
}

void GamebryoSaveGame::readFO4(GamebryoSaveGame::FileWrapper &file)
{
  file.skip<uint32_t>(); // header size
  file.skip<uint32_t>(); // header version
  file.read(m_SaveNumber);

  file.read(m_PCName);

  uint32_t temp;
  file.read(temp);
  m_PCLevel = static_cast<uint16_t>(temp);
  file.read(m_PCLocation);

  file.read(m_Playtime);   // playtime as ascii hh.mm.ss
  std::string ignore;
  file.read(ignore);   // race name (i.e. BretonRace)

  file.skip<uint16_t>(); // Player gender (0 = male)
  file.skip<float>(2);         // experience gathered, experience required

  uint64_t ftime;
  file.read(ftime);
  m_CreationTime = windowsTicksToEpoch(ftime);
  
  if (!m_QuickRead) {
    file.readImage(true);

    uint8_t formVersion;
    file.read(formVersion);
    file.read(ignore);          // game version
    file.skip<uint32_t>(); // plugin info size

    file.readPlugins();

    if (formVersion >= 0x44) {
      // lazy: just read the esls into the existing plugin list
      file.readLightPlugins();
    }
  }
}

GamebryoSaveGame::FileWrapper::FileWrapper(GamebryoSaveGame *game, CodePage encoding)
  : m_Game(game)
  , m_Decoder(new DirectDecoder(game->m_FileName))
  , m_HasFieldMarkers(false)
  , m_BZString(false)
  , m_Encoding(encoding)
{
}

bool GamebryoSaveGame::FileWrapper::header(const char *expected)
{
  std::string foundId;
  foundId.resize(strlen(expected));
  m_Decoder->seek(0);
  m_Decoder->read(&foundId[0], foundId.length());

  return foundId == expected;
}

void GamebryoSaveGame::FileWrapper::setHasFieldMarkers(bool state)
{
  m_HasFieldMarkers = state;
}

void GamebryoSaveGame::FileWrapper::setBZString(bool state)
{
  m_BZString = state;
}

void GamebryoSaveGame::FileWrapper::readBString(std::string &value)
{
  unsigned char length;
  read(length);
  std::string buffer;
  buffer.resize(length);
  read(&buffer[0], length);

  value = buffer;
}


template <> void GamebryoSaveGame::FileWrapper::read(std::string &value)
{
  unsigned short length;
  if (m_BZString) {
    unsigned char len;
    read(len);
    length = len;
  } else {
    read(length);
  }
  std::string buffer;
  if (length) {
    buffer.resize(length);
    read(&buffer[0], length);

    if (m_BZString) {
      buffer.resize(buffer.length() - 1);
    }

    if (m_HasFieldMarkers) {
      char sep;
      m_Decoder->read(&sep, 1);
      sanityCheck(sep == '|', "Expected field separator");
    }
  }

  value = toMB(toWC(buffer.c_str(), m_Encoding, length).c_str(), CodePage::UTF8, length);
}

void GamebryoSaveGame::FileWrapper::read(void *buff, std::size_t length)
{
  if (!m_Decoder->read(static_cast<char *>(buff), length)) {
    m_Decoder->clear();
    m_Decoder->seek(0, std::ios::end);
    throw std::runtime_error(fmt::format("unexpected end of file at \"{}\" (read of \"{}\" bytes)", m_Decoder->tell(), length).c_str());
  }
}

void GamebryoSaveGame::FileWrapper::readImage(bool alpha)
{
  unsigned long width;
  read(width);
  unsigned long height;
  read(height);
  readImage(width, height, alpha);
}

void GamebryoSaveGame::FileWrapper::readImage(unsigned long width, unsigned long height, bool alpha)
{
  // sanity check to prevent us from trying to open a ridiculously large buffer for the image
  sanityCheck(width < 2000, "invalid width");
  sanityCheck(height < 2000, "invalid height");

  int bpp = alpha ? 4 : 3;

  int bytes = width * height * bpp;

  std::vector<uint8_t> buffer;
  buffer.resize(bytes);

  m_Game->m_ScreenshotDim = Dimensions(width, height);

  read(&buffer[0], bytes);

  if (alpha) {
    // no postprocessing necessary
    m_Game->m_Screenshot = std::move(buffer);
  } else {
    // begin scary
    std::vector<uint8_t> rgba;
    rgba.resize(width * height * 4);
    uint8_t *in = &buffer[0];
    uint8_t *out = &rgba[0];
    uint8_t *end = in + bytes;
    for (; in < end; in += 3, out += 4) {
      memcpy(out, in, 3);
      out[3] = 0xFF;
    }
    // end scary

    m_Game->m_Screenshot = std::move(rgba);
  }
}

void GamebryoSaveGame::FileWrapper::readPlugins(bool bStrings)
{
  unsigned char count;
  read(count);
  for (std::size_t i = 0; i < count; ++i) {
    std::string name;
    if (bStrings) {
      readBString(name);
    }
    else {
      read(name);
    }
    sanityCheck(name.length() <= 256, "Invalid plugin name");
    m_Game->m_Plugins.push_back(name);
  }
}

void GamebryoSaveGame::FileWrapper::readLightPlugins()
{
  uint16_t count;
  read(count);
  for (std::size_t i = 0; i < count; ++i) {
    std::string name;
    read(name);
    sanityCheck(name.length() <= 256, "Invalid light plugin name");
    m_Game->m_Plugins.push_back(name);
  }
}

void GamebryoSaveGame::FileWrapper::setCompression(unsigned short format, unsigned long compressedSize, unsigned long uncompressedSize)
{
  if (format == 1) {
    m_Decoder.reset(new ZlibDecoder(m_Decoder, compressedSize, uncompressedSize));
  } else if (format == 2) {
    m_Decoder.reset(new LZ4Decoder(m_Decoder, compressedSize, uncompressedSize));
  }
}

void GamebryoSaveGame::FileWrapper::sanityCheck(bool conditionMatch, const char* message) {
  if (!conditionMatch) {
    throw DataInvalid(message, m_Decoder->tell());
  }
}

Napi::Value create(const Napi::CallbackInfo &info) {
  try {
    Napi::String fileName = info[0].ToString();
    Napi::Boolean quick = info[1].ToBoolean();
    Napi::Function callback = info[2].As<Napi::Function>();

    Napi::Object obj = GamebryoSaveGame::CreateNewItem(info.Env());
    GamebryoSaveGame::Unwrap(obj)->readAsync(info.Env(), fileName.Utf8Value(), quick, callback);
    return info.Env().Undefined();
  }
  catch (const std::exception& e) {
    throw Napi::Error::New(info.Env(), e.what());
  }


  /*
  Nan::AsyncQueueWorker(
    new LoadWorker(fileName, quick, new Nan::Callback(callback.getJsFunction())));
  */
}

