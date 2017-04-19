#include <apt/Image.h>

#include <apt/def.h>
#include <apt/log.h>
#include <apt/File.h>
#include <apt/FileSystem.h>
#include <apt/Time.h>

#include <cmath>
#include <cstring>

using namespace apt;

// Copy at most _srcCount objects from _src to _dst, performing conversion from 
// tSrc to tDst. If _srcCount < _dstCount, the remaining elements of _dst are
// initialized as tDst(0).
template <typename tSrc, typename tDst>
static void ConvertCopy(const tSrc* _src, tDst* _dst, uint _srcCount, uint _dstCount)
{
	do {
		*_dst = DataType::Convert<tSrc, tDst>(*_src);
		++_src;
		++_dst;
		--_dstCount;
		--_srcCount;
	} while (_dstCount != 0 && _srcCount != 0);
	while (_dstCount != 0) {
		*_dst = (tDst)0;
		++_dst;
		--_dstCount;
	}
}

// Copy image data (_size texels) from _src to _dst. _srcCount/_dstCount are the
// number of components per texel in _src/_dst respectively.
template <typename tSrc, typename tDst>
static void ConvertCopyImage(const void* _src, void* _dst, uint _srcCount, uint _dstCount, uint _size)
{
	const tSrc* src = (const tSrc*)_src;
	tDst* dst = (tDst*)_dst;
	for (uint i = 0, n = _size / _srcCount; i < n; ++i) {
		ConvertCopy(src, dst, _srcCount, _dstCount);
		src += _srcCount;
		dst += _dstCount;
	}
}

/*******************************************************************************

                                   Image

*******************************************************************************/

// PUBLIC

Image* Image::Create1d(uint _width, Layout _layout, DataType _dataType, uint _mipmapCount, CompressionType _compressionType)
{
	Image* ret = new Image;
	APT_ASSERT(ret);
	ret->init();
	ret->m_type        = Type_1d;
	ret->m_width       = _width;
	ret->m_layout      = _layout;
	ret->m_dataType    = _dataType;
	ret->m_mipmapCount = _mipmapCount;
	ret->m_compression = _compressionType;
	ret->alloc();

	return ret;
}

Image* Image::Create2d(uint _width, uint _height, Layout _layout, DataType _dataType, uint _mipmapCount, CompressionType _compressionType)
{
	Image* ret = new Image;
	APT_ASSERT(ret);
	ret->init();
	ret->m_type        = Type_2d;
	ret->m_width       = _width;
	ret->m_height      = _height;
	ret->m_layout      = _layout;
	ret->m_dataType    = _dataType;
	ret->m_mipmapCount = _mipmapCount;
	ret->m_compression = _compressionType;
	ret->alloc();

	return ret;
}


Image* Image::Create3d(uint _width, uint _height, uint _depth, Layout _layout, DataType _dataType, uint _mipmapCount, CompressionType _compressionType)
{
	Image* ret = new Image;
	APT_ASSERT(ret);
	ret->init();
	ret->m_type        = Type_3d;
	ret->m_width       = _width;
	ret->m_height      = _height;
	ret->m_depth       = _depth;
	ret->m_layout      = _layout;
	ret->m_dataType    = _dataType;
	ret->m_mipmapCount = _mipmapCount;
	ret->m_compression = _compressionType;
	ret->alloc();

	return ret;
}

void Image::Destroy(Image*& _img_)
{
	APT_ASSERT(_img_);
	if (_img_) {
		delete _img_;
		_img_ = 0;
	}
}

bool Image::Read(Image& img_, const File& _file, FileFormat _format)
{
	if (_format == FileFormat_Invalid) {
		_format = GuessFormat(_file.getPath());
		if (_format == FileFormat_Invalid) {
			APT_LOG_ERR("Image: Unsupported file format '%s'", _file.getPath());
			return false;
		}
	}

	img_.init(); // \todo don't affect the image before loading succeeded
	switch (_format) {
		case FileFormat_Dds: return ReadDds(img_, _file.getData(), _file.getDataSize());
		case FileFormat_Png: return ReadPng(img_, _file.getData(), _file.getDataSize());
		case FileFormat_Bmp:
		case FileFormat_Gif:
		case FileFormat_Hdr:
		case FileFormat_Jpg:
		case FileFormat_Psd:
		case FileFormat_Tga: return ReadDefault(img_, _file.getData(), _file.getDataSize());

		default: APT_ASSERT(false); return false; // should never happen as we check the ext above
	};

	return true;
}

bool Image::Read(Image& img_, const char* _path, FileFormat _format)
{
	APT_AUTOTIMER("Image::Read(%s)", _path);
	File f;
	f.setPath(_path);
	if (!File::Read(f, _path)) {
		return false;
	}
	return Read(img_, f, _format);
}


bool Image::Write(const Image& _img, File& file_, FileFormat _format)
{
	bool ret = false;

	if (_format == FileFormat_Invalid) {
		_format = GuessFormat(file_.getPath());
		if (_format == FileFormat_Invalid) {
			APT_LOG_ERR("Image: Unsupported file format '%s'", file_.getPath());
			goto Image_Write_end;
		}
	}

	if (!_img.validateFileFormat(_format)) {
		APT_ASSERT(false);
		goto Image_Write_end;
	}

	switch (_format) {
		case FileFormat_Bmp: ret = WriteBmp(file_, _img); break;
		case FileFormat_Dds: ret = WriteDds(file_, _img); break;
		case FileFormat_Png: ret = WritePng(file_, _img); break;
		case FileFormat_Tga: ret = WriteTga(file_, _img); break;
		case FileFormat_Hdr: ret = WriteHdr(file_, _img); break;
		default: APT_LOG_ERR("Image: File format does not supported writing '%s'", file_.getPath()); goto Image_Write_end;
	};

	ret = true;

Image_Write_end:
	APT_ASSERT(ret);
	return ret;
}

bool Image::Write(const Image& _img, const char* _path, FileFormat _format)
{
	APT_AUTOTIMER("Image::Write(%s)", _path);
	File f;
	f.setPath(_path);
	if (!Write(_img, f, _format)) {
		return false;
	}
	return File::Write(f, _path);
}

uint Image::GetMaxMipmapSize(uint _width, uint _height, uint _depth)
{
	const double rlog2    = 1.0 / log(2.0);
	const uint log2Width  = (uint)(log((double)_width)  * rlog2);
	const uint log2Height = (uint)(log((double)_height) * rlog2);
	const uint log2Depth  = (uint)(log((double)_depth)  * rlog2);
	uint mipCount = APT_MAX(log2Width, APT_MAX(log2Height, log2Depth)) + 1; // +1 for level 0
	mipCount = APT_MIN(APT_MIN(mipCount, (uint)1u), kMaxMipmapCount);
	return mipCount;
}

char* Image::getRawImage(uint _array, uint _mip) const
{
	APT_ASSERT(m_data);
	APT_ASSERT(_array < m_arrayCount);
	APT_ASSERT(_mip < m_mipmapCount);
	if (m_data == 0 || _array >= m_arrayCount || _mip >= m_mipmapCount) {
		return 0;
	}

	uint offset = _array * m_arrayLayerSize + m_mipOffsets[_mip];
	return m_data + offset;
}

uint Image::getRawImageSize(uint _mip) const
{
	APT_ASSERT(m_data);
	APT_ASSERT(_mip < m_mipmapCount);
	if (m_data == 0 || _mip >= m_mipmapCount) {
		return 0;
	}

	return m_mipSizes[_mip];
}


void Image::setRawImage(uint _array, uint _mip, const void* _src, Layout _layout, DataType _dataType, CompressionType _compressionType)
{
	APT_ASSERT(_src);

	APT_ASSERT_MSG(_compressionType == m_compression, "Compression types must match");

 // layout/type match, memcpy data
	if (_layout == m_layout && _dataType == m_dataType && _compressionType == m_compression) {
		memcpy(const_cast<char*>(getRawImage(_array, _mip)), _src, getRawImageSize(_mip));
		return;
	}

 // layout/type don't match, must convert
	uint srcCount = GetComponentCount(_layout);
	uint dstCount = GetComponentCount(m_layout);
	void* dst = const_cast<char*>(getRawImage(_array, _mip));

	#define CONVERT_FROM(type) \
		switch (m_dataType) { \
			case DataType::Uint8N:   ConvertCopyImage<DataType::ToType<DataType:: ## type>::Type, DataType::ToType<DataType::Uint8N >::Type >(_src, dst, srcCount, dstCount, m_arrayLayerSize); break; \
			case DataType::Uint16N:  ConvertCopyImage<DataType::ToType<DataType:: ## type>::Type, DataType::ToType<DataType::Uint16N>::Type >(_src, dst, srcCount, dstCount, m_arrayLayerSize); break; \
			case DataType::Uint32N:  ConvertCopyImage<DataType::ToType<DataType:: ## type>::Type, DataType::ToType<DataType::Uint32N>::Type >(_src, dst, srcCount, dstCount, m_arrayLayerSize); break; \
			case DataType::Float32:  ConvertCopyImage<DataType::ToType<DataType:: ## type>::Type, DataType::ToType<DataType::Float32>::Type >(_src, dst, srcCount, dstCount, m_arrayLayerSize); break; \
			default: APT_ASSERT(false); \
		}
	switch (_dataType) {
		case DataType::Uint8N:  CONVERT_FROM(Uint8N); break;
		case DataType::Uint16N: CONVERT_FROM(Uint16N); break;
		case DataType::Uint32N: CONVERT_FROM(Uint32N); break;
		case DataType::Float32: CONVERT_FROM(Float32); break;
		default: APT_ASSERT_MSG(false, "Unknown data type");
	};
	#undef CONVERT_FROM
}

// PRIVATE

Image::~Image()
{
	free(m_data);
}

void Image::init()
{
	m_width       = m_height = m_depth = 1;
	m_arrayCount  = 1;
	m_mipmapCount = 1;
	m_type        = Type_Invalid;
	m_compression = Compression_None;
	m_layout      = Layout_Invalid;
	m_dataType    = DataType::InvalidType;
	
	char* m_data = 0;
	memset(m_mipOffsets, 0, sizeof(uint) * kMaxMipmapCount);
	memset(m_mipSizes,   0, sizeof(uint) * kMaxMipmapCount);
	m_arrayLayerSize = 0;
	m_texelSize = 0;
}

void Image::alloc()
{
	free(m_data);
	
	m_texelSize = DataType::GetSizeBytes(m_dataType) * GetComponentCount(m_layout);
	uint w = m_width, h = m_height, d = m_depth;
	m_arrayLayerSize = 0;
	uint i = 0, lim = APT_MIN(m_mipmapCount, GetMaxMipmapSize(w, h, d));
	do {
		m_mipOffsets[i] = m_arrayLayerSize;
		m_mipSizes[i] = m_texelSize * w * h * d;
		m_arrayLayerSize += m_mipSizes[i];
		w = APT_MAX(w / 2, (uint)1);
		h = APT_MAX(h / 2, (uint)1);
		d = APT_MAX(d / 2, (uint)1);
		++i;
		APT_ASSERT(i < kMaxMipmapCount);
	} while (i < lim);

	m_data = (char*)malloc(m_arrayLayerSize * m_arrayCount);
	APT_ASSERT(m_data);
}

bool Image::validateFileFormat(FileFormat _format) const
{
	switch (_format) {
		case FileFormat_Bmp:
			if (m_compression != Compression_None) return false;
			if (DataType::IsFloat(m_dataType))  return false;
			if (DataType::IsSigned(m_dataType)) return false;
			if (!DataType::IsNormalized(m_dataType)) return false;
			if (IsDataTypeBpc(m_dataType, 16) || IsDataTypeBpc(m_dataType, 32)) return false;
			return true;
		case FileFormat_Dds:
			if (m_type == Type_3dArray) return false;
			return true;
		case FileFormat_Hdr:
			if (m_compression != Compression_None) return false;
			if (!DataType::IsFloat(m_dataType)) return false;
			if (IsDataTypeBpc(m_dataType, 8) || IsDataTypeBpc(m_dataType, 16)) return false;
			return true;
		case FileFormat_Png:
			if (m_compression != Compression_None) return false;
			if (DataType::IsFloat(m_dataType))  return false;
			if (DataType::IsSigned(m_dataType)) return false;
			if (!DataType::IsNormalized(m_dataType)) return false;
			if (IsDataTypeBpc(m_dataType, 32)) return false;
			return true;
		case FileFormat_Tga:
			if (m_compression != Compression_None) return false;
			if (DataType::IsFloat(m_dataType))  return false;
			if (DataType::IsSigned(m_dataType)) return false;
			if (!DataType::IsNormalized(m_dataType)) return false;
			if (IsDataTypeBpc(m_dataType, 16) || IsDataTypeBpc(m_dataType, 32)) return false;
			return true;
		default:
			break;
	};

	return false;
}

uint Image::GetComponentCount(Layout _layout)
{
	switch (_layout) {
		case Layout_R:         return 1;
		case Layout_RG:        return 2;
		case Layout_RGB:       return 3;
		case Layout_RGBA:      return 4;
		case Layout_Invalid:
		default:                 return 0;
	};
}

Image::Layout Image::GuessLayout(uint _cmpCount)
{
	switch (_cmpCount) {
		case 1:  return Layout_R;
		case 2:  return Layout_RG;
		case 3:  return Layout_RGB;
		case 4:  return Layout_RGBA;
		default: return Layout_Invalid;
	};
}

Image::FileFormat Image::GuessFormat(const char* _path)
{
	if        (FileSystem::CompareExtension("bmp", _path)) {
		return FileFormat_Bmp;
	} else if (FileSystem::CompareExtension("dds", _path)) {
		return FileFormat_Dds;
	} else if (FileSystem::CompareExtension("hdr", _path)) {
		return FileFormat_Hdr;
	} else if (FileSystem::CompareExtension("png", _path)) {
		return FileFormat_Png;
	} else if (FileSystem::CompareExtension("tga", _path)) {
		return FileFormat_Tga;
	} else if (FileSystem::CompareExtension("jpg", _path) || FileSystem::CompareExtension("jpeg", _path)) {
		return FileFormat_Jpg;
	} else if (FileSystem::CompareExtension("gif", _path)) {
		return FileFormat_Gif;
	} else if (FileSystem::CompareExtension("psd", _path)) {
		return FileFormat_Psd;
	}

	return FileFormat_Invalid;
}

bool Image::IsDataTypeBpc(DataType _type, int _bpc)
{
	return _bpc == DataType::GetSizeBytes(_type) * 8;
}

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_ONLY_HDR
#define STBI_ONLY_JPEG
#define STBI_ONLY_PSD
#define STBI_ONLY_TGA
#define STBI_FAILURE_USERMSG
#define STBI_ASSERT(x) APT_ASSERT(x)
#include <stb_image.h>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#define STBIW_ASSERT(x) APT_ASSERT(x)
#include <cstdio>
#include <stb_image_write.h>
static void StbiWriteFile(void* file_, void* _data, int _size)
{
 // \todo need a better approach here, as many calls to appendData is slow (it just calls realloc() on
 //  the internal buffer). Maybe use a smart reallocation scheme or allocate an initial scratch buffer
 //  with an estimated file size to reduce the number of reallocations.
	if (_size == 0) {
		return;
	}
	APT_ASSERT(file_);
	File* f = (File*)file_;
	f->appendData((const char*)_data, _size);
}

#include <lodepng.h>
static void SwapByteOrder(char* _d_, unsigned _dsize)
{
    for (unsigned i = 0; i < _dsize; i += 2) {
        unsigned j = i + 1;
        unsigned char tmp = _d_[j];
        _d_[j] = _d_[i];
        _d_[i] = tmp;
    }
}

bool Image::ReadDefault(Image& img_, const char* _data, uint _dataSize)
{
	int w, h, cmp;
	stbi_uc* d;
	bool isHdr = stbi_is_hdr_from_memory((stbi_uc*)_data, (int)_dataSize) != 0;
	if (isHdr) {
		d = (stbi_uc*)stbi_loadf_from_memory((stbi_uc*)_data, (int)_dataSize, &w, &h, &cmp, 0);
	} else {
		d = stbi_load_from_memory((stbi_uc*)_data, (int)_dataSize, &w, &h, &cmp, 0);
	}
	if (!d) {
		APT_LOG_ERR("stbi_load%s_from_memory() failed '%s'", isHdr ? "f" : "", stbi_failure_reason());
		return false;
	}
	img_.m_width       = w;
	img_.m_height      = h;
	img_.m_depth       = img_.m_arrayCount = img_.m_mipmapCount = 1;
	img_.m_type        = Type_2d;
	img_.m_layout      = GuessLayout((uint)cmp);
	img_.m_dataType    = isHdr ? DataType::Float32 : DataType::Uint8N;
	img_.m_compression = Compression_None;
	img_.alloc();
	memcpy(img_.m_data, d, w * h * cmp * (isHdr ? sizeof(float) : sizeof(stbi_uc))); // \todo avoid this, let image own the ptr
	stbi_image_free(d);
	return true;
}
bool Image::ReadPng(Image& img_, const char* _data, uint _dataSize)
{
	LodePNGDecoderSettings settings;
    lodepng_decoder_settings_init(&settings);
	settings.color_convert = 0;
    LodePNGState state;
    lodepng_state_init(&state);
    state.decoder = settings;
	DataType dataType;	
	bool ret = true;
	unsigned x, y, cmp;
	unsigned char* d;
	unsigned err = lodepng_inspect(&x, &y, &state, (unsigned char*)_data, _dataSize);
	if (err) {
		goto Image_ReadPng_end;
	}
	err = lodepng_decode(&d, &x, &y, &state, (unsigned char*)_data, _dataSize);
	if (err) {
		goto Image_ReadPng_end;
	}

	switch (state.info_raw.colortype) {
		case LCT_GREY:          cmp = 1; break;
		case LCT_GREY_ALPHA:    cmp = 2; break;
		case LCT_RGB:           cmp = 3; break;
		case LCT_RGBA:          cmp = 4; break;
		case LCT_PALETTE:
		default:                APT_ASSERT_MSG(false, "Unsupported PNG format");
		                        ret = false;
	};
	switch (state.info_raw.bitdepth) {
		case 8:                 dataType = DataType::Uint8N;  break;
		case 16:                dataType = DataType::Uint16N;
		                        SwapByteOrder((char*)d, x * y * cmp * 2); break; // \todo swizzle bytes during copy to img_
		default:                APT_ASSERT_MSG(false, "Unsupported bit depth (%d)", state.info_raw.bitdepth);
		                        ret = false;
	};

	img_.m_width       = x;
	img_.m_height      = y;
	img_.m_depth       = img_.m_arrayCount = img_.m_mipmapCount = 1;
	img_.m_type        = Type_2d;
	img_.m_layout      = GuessLayout((uint)cmp);
	img_.m_dataType    = dataType;
	img_.m_compression = Compression_None;
	img_.alloc();
	memcpy(img_.m_data, d, x * y * cmp * DataType::GetSizeBytes(dataType)); // \todo, avoid this?

Image_ReadPng_end:
	free(d);
	lodepng_state_cleanup(&state);
	if (err) {
		APT_LOG_ERR("lodepng error:\n\t'%s'", lodepng_error_text(err));
		return false;
	}
	return ret;
}
bool Image::WritePng(File& file_, const Image& _img)
{
	LodePNGColorType colorType;
	unsigned bitdepth;
	bool ret = true;

	char* buf = 0;

	switch (_img.m_layout) {
		case Layout_R:     colorType = LCT_GREY;       break;
		case Layout_RG:    colorType = LCT_GREY_ALPHA; break;
		case Layout_RGB:   colorType = LCT_RGB;        break;
		case Layout_RGBA:  colorType = LCT_RGBA;       break;
		default:           APT_ASSERT_MSG(false, "Invalid image layout");
		                   ret = false;
	};

	switch (_img.m_dataType) {
		case DataType::Uint8N:  bitdepth = 8;   break;
		case DataType::Uint16N: bitdepth = 16;
		                       // \hack \todo Can lodepng be modified to swizzle the bytes automatically on x86?
	                            buf = (char*)malloc(_img.getRawImageSize());
	                            memcpy(buf, _img.getRawImage(), _img.getRawImageSize());
		                        SwapByteOrder(buf, (unsigned)_img.getRawImageSize()); break;
		default:                APT_ASSERT_MSG(false, "Unsupported data type");
		                        ret = false;
	};

 // don't use the default encoder state, which ignores colorType in some cases
	LodePNGEncoderSettings enc;
	lodepng_encoder_settings_init(&enc);
	enc.auto_convert = false;
	LodePNGState state;
	lodepng_state_init(&state);
	state.info_raw.colortype = colorType;
	state.info_raw.bitdepth = bitdepth;
	state.encoder = enc;

	unsigned char* d;
	uint dsize;
	unsigned err = lodepng_encode(&d, &dsize, (unsigned char*)(buf ? buf : _img.getRawImage()), (unsigned)_img.getWidth(), (unsigned)_img.getHeight(), &state);
	if (err) {
		goto Image_WritePng_end;
	}
	file_.setData((const char*)d, dsize);

Image_WritePng_end:
	lodepng_state_cleanup(&state);
	free(buf);
	free(d);
	if (err) {
		APT_LOG_ERR("lodepng error:\n\t'%s'", lodepng_error_text(err));
		return false;
	}
	return ret;
}
bool Image::WriteBmp(File& file_, const Image& _img)
{
	if (!stbi_write_bmp_to_func(StbiWriteFile, &file_, (int)_img.m_width, (int)_img.m_height, (int)GetComponentCount(_img.m_layout), _img.m_data)) {
		APT_LOG_ERR("stbi_write_bmp_to_func() failed");
		return false;
	}
	return true;
}
bool Image::WriteTga(File& file_, const Image& _img)
{
	if (!stbi_write_tga_to_func(StbiWriteFile, &file_, (int)_img.m_width, (int)_img.m_height, (int)GetComponentCount(_img.m_layout), _img.m_data)) {
		APT_LOG_ERR("stbi_write_tga_to_func() failed");
		return false;
	}
	return true;
}
bool Image::WriteHdr(File& file_, const Image& _img)
{
	if (!stbi_write_hdr_to_func(StbiWriteFile, &file_, (int)_img.m_width, (int)_img.m_height, (int)GetComponentCount(_img.m_layout), (float*)_img.m_data)) {
		APT_LOG_ERR("stbi_write_hdr_to_func() failed");
		return false;
	}
	return true;
}
