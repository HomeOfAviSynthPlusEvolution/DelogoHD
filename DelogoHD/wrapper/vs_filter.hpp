#include <VapourSynth.h>
#include <VSHelper.h>

class VSWrapperFrame : public WrapperFrame {
protected:
  VSCore *_core;
  const VSAPI * _vsapi;
  bool readonly;
public:
  VSFrameRef * _frame;
  VSWrapperFrame(VSFrameRef * frame, VSCore *core, const VSAPI *vsapi)
    : _core(core), _vsapi(vsapi), _frame(frame), readonly(false) {
  }
  VSWrapperFrame(const VSFrameRef * frame, VSCore *core, const VSAPI *vsapi)
    : _core(core), _vsapi(vsapi), _frame((VSFrameRef*)frame), readonly(true) {
  }
  VSWrapperFrame* dup() {
    return new VSWrapperFrame(_vsapi->copyFrame(_frame, _core), _core, _vsapi);
  }
  int stride(int plane = 0) const { return _vsapi->getStride(_frame, plane); }
  int width (int plane = 0) const { return _vsapi->getFrameWidth(_frame, plane); }
  int height(int plane = 0) const { return _vsapi->getFrameHeight(_frame, plane); }
  unsigned char* writeptr(int plane = 0) const
    { return _vsapi->getWritePtr(_frame, plane); }
  const unsigned char* readptr(int plane = 0) const
    { return _vsapi->getReadPtr(_frame, plane); }
};

class VSWrapperClip : public WrapperClip {
protected:
  VSCore *_core;
  const VSAPI * _vsapi;
public:
  VSNodeRef * _clip;
  VSVideoInfo _vi;
  VSWrapperClip(VSNodeRef * clip, VSCore *core, const VSAPI *vsapi)
    : _core(core), _vsapi(vsapi), _clip(clip) {
    _vi = *_vsapi->getVideoInfo(_clip);
  }
  VSVideoInfo vi() {
    return _vi;
  }
  VSWrapperFrame* getFrame(int n) {
    return new VSWrapperFrame(_vsapi->getFrame(n, _clip, nullptr, 0), _core, _vsapi);
  }

  int width()     const { return _vi.width; }
  int height()    const { return _vi.height; }
  int ssw()       const { return _vi.format->subSamplingW; }
  int ssh()       const { return _vi.format->subSamplingH; }
  bool hasVideo() const { return _vi.format != nullptr; }
  bool hasAudio() const { return false; } // Who cares?

  bool isRGB()    const { return _vi.format->colorFamily == cmRGB; }
  bool isYUV()    const { return _vi.format->colorFamily == cmYUV; }

  bool isRGB24()  const { return isRGB(); } // Who cares?
  bool isRGB32()  const { return isRGB(); } // Who cares?
  bool isYV24()   const {
    return isYUV()
      && _vi.format->subSamplingW == 0
      && _vi.format->subSamplingH == 0;
  }
  bool isYV16()   const {
    return isYUV()
      && _vi.format->subSamplingW == 1
      && _vi.format->subSamplingH == 0;
  }
  bool isYV12()   const {
    return isYUV()
      && _vi.format->subSamplingW == 1
      && _vi.format->subSamplingH == 1;
  }
  bool isY8()     const { return _vi.format->colorFamily == cmGray; }

};

class VSFilter {
protected:
  void initialize() {};
  const char* name() const { return "VSFilter"; };
  WrapperFrame* get(int n) {
    return child->getFrame(n);
  }

public:
  VSVideoInfo vi;
  VSWrapperClip* child;
  const VSMap *_in;
  VSCore * _core;
  const VSAPI * _vsapi;

  VSFilter(const VSMap *in, VSMap *out, VSCore *core, const VSAPI *vsapi)
    : _in(in), _core(core), _vsapi(vsapi) {
    child = ArgAsClip(0, "clip");
    vi = child->vi();
    try {
      initialize();
    }
    catch(const char *err){
      char msg_buff[256];
      snprintf(msg_buff, 256, "%s(" PLUGIN_VERSION "): %s", name(), err);
      vsapi->setError(out, msg_buff);
      throw(err);
    }
  }

  VSWrapperClip* ArgAsClip(int, const char* name) const { return new VSWrapperClip(_vsapi->propGetNode(_in, name, 0, 0), _core, _vsapi); }

  bool ArgAsBool(int, const char* name) const {
    return ArgAsInt(0, name) != 0;
  }
  int ArgAsInt(int, const char* name) const {
    int _err;
    auto data = int64ToIntS(_vsapi->propGetInt(_in, name, 0, &_err));
    if (_err) throw(name);
    return data;
  }
  double ArgAsFloat(int, const char* name) const {
    int _err;
    auto data = _vsapi->propGetFloat(_in, name, 0, &_err);
    if (_err) throw(name);
    return data;
  }
  const char* ArgAsString(int, const char* name) const {
    int _err;
    auto data = _vsapi->propGetData(_in, name, 0, &_err);
    if (_err) throw(name);
    return data;
  }

  bool ArgAsBool(int, const char* name, bool def) const {
    return ArgAsInt(0, name, def ? 1 : 0) != 0;
  }
  int ArgAsInt(int, const char* name, int def) const {
    int _err;
    auto data = int64ToIntS(_vsapi->propGetInt(_in, name, 0, &_err));
    if (_err) return def;
    return data;
  }
  double ArgAsFloat(int, const char* name, float def) const {
    int _err;
    auto data = _vsapi->propGetFloat(_in, name, 0, &_err);
    if (_err) return def;
    return data;
  }
  const char* ArgAsString(int, const char* name, const char* def) const {
    int _err;
    auto data = _vsapi->propGetData(_in, name, 0, &_err);
    if (_err) return def;
    return data;
  }

  void VS_CC GetFramePre(VSFrameContext *_frameCtx, VSCore *_core, const VSAPI *vsapi, int n) {
    vsapi->requestFrameFilter(n, child->_clip, _frameCtx);
  }

  const VSFrameRef *VS_CC GetFrame(VSFrameContext *_frameCtx, VSCore *_core, const VSAPI *vsapi, int n) {
    auto frame = dynamic_cast<VSWrapperFrame*>(get(n));
    return frame->_frame;
  }

  typedef VSFrameRef * Wrapper;
};
