#include <kore3/video.h>

#ifdef KORE_DIRECT3D12

void kore_video_init(kore_video *video, const char *filename) {}

void kore_video_destroy(kore_video *video) {}

kore_gpu_texture *kore_video_current_image(kore_video *video) {
	return NULL;
}

int kore_video_width(kore_video *video) {
	return 64;
}

int kore_video_height(kore_video *video) {
	return 64;
}

void kore_video_play(kore_video *video, bool loop) {}

void kore_video_pause(kore_video *video) {}

void kore_video_stop(kore_video *video) {}

void kore_video_update(kore_video *video, double time) {}

double kore_video_duration(kore_video *video) {
	return 0.0;
}

double kore_video_position(kore_video *video) {
	return 0.0;
}

bool kore_video_finished(kore_video *video) {
	return true;
}

bool kore_video_paused(kore_video *video) {
	return true;
}

#else

#ifndef KORE_NO_DIRECTSHOW

#include <streams.h>

namespace {
	IGraphBuilder  *graphBuilder;
	IMediaControl  *mediaControl;
	IMediaPosition *mediaPosition;
	IMediaEvent    *mediaEvent;

	struct __declspec(uuid("{71771540-2017-11cf-ae24-0020afd79767}")) CLSID_TextureRenderer;
}

class CTextureRenderer : public CBaseVideoRenderer {
public:
	CTextureRenderer(LPUNKNOWN pUnk, HRESULT *phr);
	~CTextureRenderer();

public:
	HRESULT CheckMediaType(const CMediaType *pmt);      // Format acceptable?
	HRESULT SetMediaType(const CMediaType *pmt);        // Video format notification
	HRESULT DoRenderSample(IMediaSample *pMediaSample); // New video sample

	// BOOL m_bUseDynamicTextures;
	// LONG m_lVidWidth;   // Video width
	// LONG m_lVidHeight;  // Video Height
	// LONG m_lVidPitch;   // Video Pitch

	kore_gpu_texture image;
	int              width;
	int              height;
	uint8_t         *pixels;
};

CTextureRenderer::CTextureRenderer(LPUNKNOWN pUnk, HRESULT *phr) : CBaseVideoRenderer(__uuidof(CLSID_TextureRenderer), TEXT("Texture Renderer"), pUnk, phr) {
	// Store and AddRef the texture for our use.
	ASSERT(phr);
	if (phr)
		*phr = S_OK;
}

CTextureRenderer::~CTextureRenderer() {
	// Do nothing
}

HRESULT CTextureRenderer::CheckMediaType(const CMediaType *pmt) {
	HRESULT    hr  = E_FAIL;
	VIDEOINFO *pvi = 0;

	CheckPointer(pmt, E_POINTER);

	// Reject the connection if this is not a video type
	if (*pmt->FormatType() != FORMAT_VideoInfo) {
		return E_INVALIDARG;
	}

	// Only accept RGB24 video
	pvi = (VIDEOINFO *)pmt->Format();

	if (IsEqualGUID(*pmt->Type(), MEDIATYPE_Video) && IsEqualGUID(*pmt->Subtype(), MEDIASUBTYPE_RGB24)) {
		hr = S_OK;
	}

	return hr;
}

HRESULT CTextureRenderer::SetMediaType(const CMediaType *pmt) {
	VIDEOINFO *info = (VIDEOINFO *)pmt->Format();
	width           = info->bmiHeader.biWidth;
	height          = abs(info->bmiHeader.biHeight);
	// kore_gpu_texture_init(&image, width, height, KORE_IMAGE_FORMAT_RGBA32); // TODO
	pixels = (uint8_t *)malloc(width * height * 3);

	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			pixels[y * width * 3 + x * 3 + 0] = 0;
			pixels[y * width * 3 + x * 3 + 1] = 0;
			pixels[y * width * 3 + x * 3 + 2] = 0;
		}
	}

	return S_OK;
}

HRESULT CTextureRenderer::DoRenderSample(IMediaSample *sample) {
	BYTE *videoPixels;
	sample->GetPointer(&videoPixels);
	int videoPitch = (width * 3 + 3) & ~(3);
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			pixels[y * width * 3 + x * 3 + 0] = videoPixels[(height - y - 1) * videoPitch + x * 3 + 2];
			pixels[y * width * 3 + x * 3 + 1] = videoPixels[(height - y - 1) * videoPitch + x * 3 + 1];
			pixels[y * width * 3 + x * 3 + 2] = videoPixels[(height - y - 1) * videoPitch + x * 3 + 0];
		}
	}
	return S_OK;
}

void kore_video_init(kore_video *video, const char *filename) {
	video->impl.duration = 1000 * 10;
	video->impl.position = 0;
	video->impl.finished = false;
	video->impl.paused   = false;
	// image = new Graphics4::Texture(100, 100, Graphics4::Image::RGBA32, false);

	HRESULT      hr = S_OK;
	IBaseFilter *pFSrc;       // Source Filter
	IPin        *pFSrcPinOut; // Source Filter Output Pin

	hr                   = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC, __uuidof(IGraphBuilder), (void **)&graphBuilder);
	video->impl.renderer = new CTextureRenderer(NULL, &hr);
	hr                   = graphBuilder->AddFilter((CTextureRenderer *)video->impl.renderer, L"TEXTURERENDERER");
	wchar_t wideFilename[2048];
	mbstowcs(wideFilename, filename, 2048 - 1);
	hr = graphBuilder->AddSourceFilter(wideFilename, L"SOURCE", &pFSrc);
	hr = pFSrc->FindPin(L"Output", &pFSrcPinOut);
	hr = graphBuilder->Render(pFSrcPinOut);

	graphBuilder->QueryInterface(&mediaControl);
	graphBuilder->QueryInterface(&mediaPosition);
	graphBuilder->QueryInterface(&mediaEvent);

	mediaPosition->get_Duration(&video->impl.duration);
	video->impl.position = 0;
}

void kore_video_destroy(kore_video *video) {}

kore_gpu_texture *kore_video_current_image(kore_video *video) {
	CTextureRenderer *renderer = (CTextureRenderer *)video->impl.renderer;
	// TODO
	/*uint8_t *pixels = kore_g4_texture_lock(&renderer->image);
	int stride = kore_g4_texture_stride(&renderer->image);
	for (int y = 0; y < renderer->height; ++y) {
	    for (int x = 0; x < renderer->width; ++x) {
	        pixels[y * stride + x * 4 + 0] = renderer->pixels[y * renderer->width * 3 + x * 3 + 0];
	        pixels[y * stride + x * 4 + 1] = renderer->pixels[y * renderer->width * 3 + x * 3 + 1];
	        pixels[y * stride + x * 4 + 2] = renderer->pixels[y * renderer->width * 3 + x * 3 + 2];
	        pixels[y * stride + x * 4 + 3] = 255;
	    }
	}
	kore_g4_texture_unlock(&renderer->image);*/

	mediaPosition->get_CurrentPosition(&video->impl.position);

	return &renderer->image;
}

int kore_video_width(kore_video *video) {
	CTextureRenderer *renderer = (CTextureRenderer *)video->impl.renderer;
	return renderer->width;
}

int kore_video_height(kore_video *video) {
	CTextureRenderer *renderer = (CTextureRenderer *)video->impl.renderer;
	return renderer->height;
}

void kore_video_play(kore_video *video, bool loop) {
	mediaControl->Run();
}

void kore_video_pause(kore_video *video) {
	mediaControl->Pause();
}

void kore_video_stop(kore_video *video) {
	mediaControl->Stop();
}

void kore_video_update(kore_video *video, double time) {
	mediaPosition->put_CurrentPosition(time);
}

double kore_video_duration(kore_video *video) {
	return video->impl.duration;
}

double kore_video_position(kore_video *video) {
	return video->impl.position;
}

bool kore_video_finished(kore_video *video) {
	return video->impl.finished;
}

bool kore_video_paused(kore_video *video) {
	return video->impl.paused;
}

#else

void kore_video_init(kore_video *video, const char *filename) {}

void kore_video_destroy(kore_video *video) {}

kore_gpu_texture *kore_video_current_image(kore_video *video) {
	return NULL;
}

int kore_video_width(kore_video *video) {
	return 64;
}

int kore_video_height(kore_video *video) {
	return 64;
}

void kore_video_play(kore_video *video, bool loop) {}

void kore_video_pause(kore_video *video) {}

void kore_video_stop(kore_video *video) {}

void kore_video_update(kore_video *video, double time) {}

double kore_video_duration(kore_video *video) {
	return 0.0;
}

double kore_video_position(kore_video *video) {
	return 0.0;
}

bool kore_video_finished(kore_video *video) {
	return true;
}

bool kore_video_paused(kore_video *video) {
	return true;
}

#endif

#endif

void kore_internal_video_sound_stream_init(kore_internal_video_sound_stream *stream, int channel_count, int frequency) {}

void kore_internal_video_sound_stream_destroy(kore_internal_video_sound_stream *stream) {}

void kore_internal_video_sound_stream_insert_data(kore_internal_video_sound_stream *stream, float *data, int sample_count) {}

static float samples[2];

float *kore_internal_video_sound_stream_next_frame(kore_internal_video_sound_stream *stream) {
	samples[0] = 0.0f;
	samples[1] = 0.0f;
	return samples;
}

bool kore_internal_video_sound_stream_ended(kore_internal_video_sound_stream *stream) {
	return true;
}
