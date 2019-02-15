#pragma once

#include <string>
#include <fstream>
#include <vector>
#include <memory>
#include <nan.h>
#include "fmt/format.h"
#include "nbind/nbind.h"


/**
 * Stores a screenshot in 32-bit rgba format
 * (storing an alpha channels seems pointless but the javascript side probably needs it
 *  and fallout4 contains an alpha channel anyway so this minimizes conversion steps. probably)
 */
class Dimensions
{
public:
  Dimensions() {}

  Dimensions(uint32_t width, uint32_t height)
    : m_Width(width), m_Height(height) {}

  uint32_t width() const { return m_Width; }
  uint32_t height() const { return m_Height; }

  void toJS(nbind::cbOutput output) {
    output(m_Width, m_Height);
  }

private:
  uint32_t m_Width;
  uint32_t m_Height;
};

class IDecoder {
public:
  virtual ~IDecoder() {};
  virtual bool seek(size_t offset, std::ios_base::seekdir dir = std::ios::beg) = 0;
  virtual size_t tell() = 0;
  virtual bool read(char *buffer, size_t size) = 0;
};

void create(const std::string &fileName, bool quick, nbind::cbFunction callback);

class GamebryoSaveGame
{
public:
  GamebryoSaveGame(const std::string &fileName, bool quick);

  virtual ~GamebryoSaveGame();

  // creation time in seconds since the unix epoch
  uint32_t creationTime() const { return m_CreationTime; }  
  std::string characterName() const { return m_PCName; }
  uint16_t characterLevel() const { return m_PCLevel; }
  std::string location() const { return m_PCLocation; }
  uint32_t saveNumber() const { return m_SaveNumber; }
  std::vector<std::string> plugins() const { return m_Plugins; }
  Dimensions screenshotSize() const { return m_ScreenshotDim; }
  
  void getScreenshot(nbind::Buffer buffer) const {
    uint8_t *outData = buffer.data();
    memcpy(outData, m_Screenshot.data(), (std::min)(buffer.length(), m_Screenshot.size()));
  }

  const std::vector<uint8_t> &screenshotData() const {
    return m_Screenshot;
  }

  std::string fileName() const { return m_FileName; }

private:

  friend class FileWrapper;

  class FileWrapper
  {
  public:
    /** Construct the save file information.
     * @params expected - expect bytes at start of file
     **/
    FileWrapper(GamebryoSaveGame *game);

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
        throw std::runtime_error(fmt::format("unexpected end of file at {} (skip of {} bytes)", m_Decoder->tell(), count * sizeof(T)).c_str());
      }
    }

    template <typename T> void read(T &value)
    {
      if (!m_Decoder->read(reinterpret_cast<char*>(&value), sizeof(T))) {
        throw std::runtime_error(fmt::format("unexpected end of file at {} (read of {} bytes)", m_Decoder->tell(), sizeof(T)).c_str());
      }
      if (m_HasFieldMarkers) {
        skip<char>();
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

  private:
    GamebryoSaveGame *m_Game;
    std::shared_ptr<IDecoder> m_Decoder;
    bool m_HasFieldMarkers;
    bool m_BZString;
  };

  void readOblivion(FileWrapper &file);
  void readSkyrim(FileWrapper &file);
  void readFO3(FileWrapper &file);
  void readFO4(FileWrapper &file);

private:

  bool m_QuickRead;
  std::string m_FileName;
  std::string m_PCName;
  uint16_t m_PCLevel;
  std::string m_PCLocation;
  uint32_t m_SaveNumber;
  uint32_t m_CreationTime;
  std::vector<std::string> m_Plugins;
  Dimensions m_ScreenshotDim;
  std::vector<uint8_t> m_Screenshot;
};

template <> void GamebryoSaveGame::FileWrapper::read<std::string>(std::string &);


v8::Local<v8::String> operator "" _n(const char *input, size_t) {
  return Nan::New(input).ToLocalChecked();
}

NBIND_CLASS(Dimensions) {
  getter(width);
  getter(height);
}

NBIND_CLASS(GamebryoSaveGame) {
  construct<std::string, bool>();
  getter(characterName);
  getter(characterLevel);
  getter(location);
  getter(saveNumber);
  getter(plugins);
  getter(creationTime);
  getter(fileName);
  getter(screenshotSize);
  method(getScreenshot);
}

NBIND_GLOBAL() {
  function(create);
}

