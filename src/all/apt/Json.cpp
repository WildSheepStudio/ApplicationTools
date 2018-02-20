#include <apt/Json.h>

#include <apt/log.h>
#include <apt/math.h>
#include <apt/FileSystem.h>
#include <apt/String.h>
#include <apt/Time.h>

#include <EASTL/vector.h>

#include <cstring>

#define RAPIDJSON_ASSERT(x) APT_ASSERT(x)
#define RAPIDJSON_PARSE_DEFAULT_FLAGS (kParseFullPrecisionFlag | kParseCommentsFlag | kParseTrailingCommasFlag)
#include <rapidjson/error/en.h>
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>

using namespace apt;

static Json::ValueType GetValueType(rapidjson::Type _type)
{
	switch (_type) {
	case rapidjson::kNullType:   return Json::ValueType_Null;
	case rapidjson::kObjectType: return Json::ValueType_Object;
	case rapidjson::kArrayType:  return Json::ValueType_Array;
	case rapidjson::kFalseType:
	case rapidjson::kTrueType:   return Json::ValueType_Bool;
	case rapidjson::kNumberType: return Json::ValueType_Number;
	case rapidjson::kStringType: return Json::ValueType_String;
	default: APT_ASSERT(false); break;
	};

	return Json::ValueType_Count;
}

/*******************************************************************************
Json
*******************************************************************************/

struct Json::Impl
{
	rapidjson::Document m_dom;

	// current value set after find()
	rapidjson::Value* m_value = nullptr;

	// value stack for objects/arrays
	eastl::vector<eastl::pair<rapidjson::Value*, int> > m_stack;

	void push(rapidjson::Value* _val = nullptr)
	{
		APT_ASSERT(m_stack.empty() || top() != _val); // probably a mistake, called push() twice?
		m_stack.push_back(eastl::make_pair(_val ? _val : m_value, 0));
	}
	void pop()
	{
		APT_ASSERT(!m_stack.empty());
		m_stack.pop_back();
	}
	rapidjson::Value* top()
	{
		APT_ASSERT(!m_stack.empty());
		return m_stack.back().first;
	}
	int& topIter()
	{
		APT_ASSERT(!m_stack.empty());
		return m_stack.back().second;
	}

	// Get the current value, optionally access the element _i if an array.
	rapidjson::Value* get(int _i = -1)
	{
		rapidjson::Value* ret = m_value;
		APT_ASSERT(ret);
		if (_i >= 0 && GetValueType(ret->GetType()) == ValueType_Array) {
			int n = (int)ret->GetArray().Size();
			APT_ASSERT_MSG(_i < n, "Array index out of bounds (%d/%d)", _i, n);
			ret = &ret->GetArray()[_i];
		}
		return ret;
	}
};


// PUBLIC

bool Json::Read(Json& json_, const File& _file)
{
	json_.m_impl->m_dom.Parse(_file.getData());
	if (json_.m_impl->m_dom.HasParseError()) {
		APT_LOG_ERR("Json error: %s\n\t'%s'", _file.getPath(), rapidjson::GetParseError_En(json_.m_impl->m_dom.GetParseError()));
		return false;
	}
	return true;
}

bool Json::Read(Json& json_, const char* _path, FileSystem::RootType _rootHint)
{
	APT_AUTOTIMER("Json::Read(%s)", _path);
	File f;
	if (!FileSystem::ReadIfExists(f, _path, _rootHint)) {
		return false;
	}
	return Read(json_, f);
}

bool Json::Write(const Json& _json, File& file_)
{
	rapidjson::StringBuffer buf;
	rapidjson::PrettyWriter<rapidjson::StringBuffer> wr(buf);
	wr.SetIndent('\t', 1);
	wr.SetFormatOptions(rapidjson::kFormatSingleLineArray);
	_json.m_impl->m_dom.Accept(wr);
	file_.setData(buf.GetString(), buf.GetSize());
	return true;
}

bool Json::Write(const Json& _json, const char* _path, FileSystem::RootType _rootHint)
{
	APT_AUTOTIMER("Json::Write(%s)", _path);
	File f;
	if (Write(_json, f)) {
		return FileSystem::Write(f, _path, _rootHint);
	}
	return false;
}

Json::Json(const char* _path, FileSystem::RootType _rootHint)
	: m_impl(nullptr)
{
	m_impl = new Impl;
	m_impl->m_dom.SetObject();
	m_impl->push(&m_impl->m_dom);

	if (_path) {
		Json::Read(*this, _path, _rootHint);
	}
}

Json::~Json()
{
	if (m_impl) {
		delete m_impl;
	}
}

bool Json::find(const char* _name)
{
	rapidjson::Value* top = m_impl->top();

	if (!top->IsObject()) {
		return false;
	}
	auto it = top->FindMember(_name);
	if (it != top->MemberEnd()) {
		m_impl->m_value = &it->value;
		return true;
	}
	return false;
}

bool Json::next()
{
	rapidjson::Value* top = m_impl->top();

	if (GetValueType(top->GetType()) == ValueType_Array) {
		auto it = top->Begin() + (m_impl->topIter()++);
		m_impl->m_value = it;
		return it != top->End();
	}
	else if (GetValueType(top->GetType()) == ValueType_Object) {
		auto it = top->MemberBegin() + (m_impl->topIter()++);
		m_impl->m_value = &it->value;
		return it != top->MemberEnd();
	}
	APT_ASSERT(false); // not an object or an array
	return false;
}

Json::ValueType Json::getType() const
{
	return GetValueType(m_impl->m_value->GetType());
}

template <> bool Json::getValue<bool>(int _i) const
{
	const rapidjson::Value* jsonValue = m_impl->get(_i);
	APT_ASSERT_MSG(GetValueType(jsonValue->GetType()) == ValueType_Bool, "Json::getValue: not a bool");
	return jsonValue->GetBool();
}

template <> sint64 Json::getValue<sint64>(int _i) const
{
	const rapidjson::Value* jsonValue = m_impl->get(_i);
	APT_ASSERT_MSG(GetValueType(jsonValue->GetType()) == ValueType_Number, "Json::getValue: not a number");
	return jsonValue->GetInt64();
}
template <> sint32 Json::getValue<sint32>(int _i) const
{
	const rapidjson::Value* jsonValue = m_impl->get(_i);
	APT_ASSERT_MSG(GetValueType(jsonValue->GetType()) == ValueType_Number, "Json::getValue: not a number");
	return jsonValue->GetInt();
}
template <> sint16 Json::getValue<sint16>(int _i) const
{
	const rapidjson::Value* jsonValue = m_impl->get(_i);
	APT_ASSERT_MSG(GetValueType(jsonValue->GetType()) == ValueType_Number, "Json::getValue: not a number");
	return jsonValue->GetInt();
}
template <> sint8 Json::getValue<sint8>(int _i) const
{
	const rapidjson::Value* jsonValue = m_impl->get(_i);
	APT_ASSERT_MSG(GetValueType(jsonValue->GetType()) == ValueType_Number, "Json::getValue: not a number");
	return jsonValue->GetInt();
}
template <> uint64 Json::getValue<uint64>(int _i) const
{
	const rapidjson::Value* jsonValue = m_impl->get(_i);
	APT_ASSERT_MSG(GetValueType(jsonValue->GetType()) == ValueType_Number, "Json::getValue: not a number");
	return jsonValue->GetUint64();
}
template <> uint32 Json::getValue<uint32>(int _i) const
{
	const rapidjson::Value* jsonValue = m_impl->get(_i);
	APT_ASSERT_MSG(GetValueType(jsonValue->GetType()) == ValueType_Number, "Json::getValue: not a number");
	return jsonValue->GetUint();
}
template <> uint16 Json::getValue<uint16>(int _i) const
{
	const rapidjson::Value* jsonValue = m_impl->get(_i);
	APT_ASSERT_MSG(GetValueType(jsonValue->GetType()) == ValueType_Number, "Json::getValue: not a number");
	return jsonValue->GetUint();
}
template <> uint8 Json::getValue<uint8>(int _i) const
{
	const rapidjson::Value* jsonValue = m_impl->get(_i);
	APT_ASSERT_MSG(GetValueType(jsonValue->GetType()) == ValueType_Number, "Json::getValue: not a number");
	return jsonValue->GetUint();
}
template <> float32 Json::getValue<float32>(int _i) const
{
	const rapidjson::Value* jsonValue = m_impl->get(_i);
	APT_ASSERT_MSG(GetValueType(jsonValue->GetType()) == ValueType_Number, "Json::getValue: not a number");
	return jsonValue->GetFloat();
}
template <> float64 Json::getValue<float64>(int _i) const
{
	const rapidjson::Value* jsonValue = m_impl->get(_i);
	APT_ASSERT_MSG(GetValueType(jsonValue->GetType()) == ValueType_Number, "Json::getValue: not a number");
	return jsonValue->GetDouble();
}
template <> const char* Json::getValue<const char*>(int _i) const
{
	const rapidjson::Value* jsonValue = m_impl->get(_i);
	APT_ASSERT_MSG(GetValueType(jsonValue->GetType()) == ValueType_String, "Json::getValue: not a string");
	return jsonValue->GetString();
}
template <> vec2 Json::getValue<vec2>(int _i) const
{
	const rapidjson::Value* jsonValue = m_impl->get(_i);
	APT_ASSERT_MSG(GetValueType(jsonValue->GetType()) == ValueType_Array, "Json::getValue: not an array");
	APT_ASSERT_MSG(jsonValue->Size() == 2, "Json::getValue: invalid vec2, size = %d", jsonValue->Size());
	auto& arr = jsonValue->GetArray();
	return vec2(arr[0].GetFloat(), arr[1].GetFloat());
}
template <> vec3 Json::getValue<vec3>(int _i) const
{
	const rapidjson::Value* jsonValue = m_impl->get(_i);
	APT_ASSERT_MSG(GetValueType(jsonValue->GetType()) == ValueType_Array, "Json::getValue: not an array");
	APT_ASSERT_MSG(jsonValue->Size() == 3, "Json::getValue: invalid vec3, size = %d", jsonValue->Size());
	auto& arr = jsonValue->GetArray();
	return vec3(arr[0].GetFloat(), arr[1].GetFloat(), arr[2].GetFloat());
}
template <> vec4 Json::getValue<vec4>(int _i) const
{
	const rapidjson::Value* jsonValue = m_impl->get(_i);
	APT_ASSERT_MSG(GetValueType(jsonValue->GetType()) == ValueType_Array, "Json::getValue: not an array");
	APT_ASSERT_MSG(jsonValue->Size() == 4, "Json::getValue: invalid vec4, size = %d", jsonValue->Size());
	auto& arr = jsonValue->GetArray();
	return vec4(arr[0].GetFloat(), arr[1].GetFloat(), arr[2].GetFloat(), arr[3].GetFloat());
}
template <> mat2 Json::getValue<mat2>(int _i) const
{
	rapidjson::Value* jsonValue = m_impl->get(_i);
	APT_ASSERT_MSG(GetValueType(jsonValue->GetType()) == ValueType_Array, "Json::getValue: not an array");
	APT_ASSERT_MSG(jsonValue->Size() == 2, "Json::getValue: invalid mat2, size = %d (should be 2* vec2)", jsonValue->Size());
	mat2 ret;
	m_impl->push();
	m_impl->m_value = jsonValue;
	for (int i = 0; i < 2; ++i) {
		ret[i] = getValue<vec2>(i);
	}
	m_impl->m_value = m_impl->top();
	m_impl->pop();
	return ret;
}
template <> mat3 Json::getValue<mat3>(int _i) const
{
	rapidjson::Value* jsonValue = m_impl->get(_i);
	APT_ASSERT_MSG(GetValueType(jsonValue->GetType()) == ValueType_Array, "Json::getValue: not an array");
	APT_ASSERT_MSG(jsonValue->Size() == 3, "Json::getValue: invalid mat3, size = %d (should be 3* vec3)", jsonValue->Size());
	mat3 ret;
	m_impl->push();
	m_impl->m_value = jsonValue;
	for (int i = 0; i < 3; ++i) {
		ret[i] = getValue<vec3>(i);
	}
	m_impl->m_value = m_impl->top();
	m_impl->pop();
	return ret;
}
template <> mat4 Json::getValue<mat4>(int _i) const
{
	rapidjson::Value* jsonValue = m_impl->get(_i);
	APT_ASSERT_MSG(GetValueType(jsonValue->GetType()) == ValueType_Array, "Json::getValue: not an array");
	APT_ASSERT_MSG(jsonValue->Size() == 4, "Json::getValue: invalid mat4, size = %d (should be 4* vec4)", jsonValue->Size());
	mat4 ret;
	m_impl->push();
	m_impl->m_value = jsonValue;
	for (int i = 0; i < 4; ++i) {
		ret[i] = getValue<vec4>(i);
	}
	m_impl->m_value = m_impl->top();
	m_impl->pop();
	return ret;
}

bool Json::enterObject()
{
	if (getType() == ValueType_Object) {
		m_impl->push();
		return true;
	}
	APT_ASSERT(false); // not an object
	return false;
}

void Json::leaveObject()
{
	APT_ASSERT(GetValueType(m_impl->top()->GetType()) == ValueType_Object);
	m_impl->m_value = m_impl->top();
	m_impl->pop();
}

bool Json::enterArray()
{
	if (getType() == ValueType_Array) {
		m_impl->push();
		return true;
	}
	APT_ASSERT(false); // not an array
	return false;
}

void Json::leaveArray()
{
	APT_ASSERT(GetValueType(m_impl->top()->GetType()) == ValueType_Array);
	m_impl->m_value = m_impl->top();
	m_impl->pop();
}

int Json::getArrayLength() const
{
	if (GetValueType(m_impl->top()->GetType()) == ValueType_Array) {
		return (int)m_impl->top()->GetArray().Size();
	}
	return -1;
}

void Json::beginObject(const char* _name)
{
	if (_name && find(_name)) {
		// object already existed, check the type
		APT_ASSERT(GetValueType(m_impl->m_value->GetType()) == ValueType_Object);
		return;
	}
	else {
		if (GetValueType(m_impl->top()->GetType()) == ValueType_Array) {
			if (_name) {
				APT_LOG("Json warning: calling beginObject() in an array, name '%s' will be ignored", _name);
			}
			m_impl->top()->PushBack(
				rapidjson::Value(rapidjson::kObjectType).Move(),
				m_impl->m_dom.GetAllocator()
			);
			m_impl->m_value = m_impl->top()->End() - 1;

		}
		else {
			m_impl->top()->AddMember(
				rapidjson::StringRef(_name),
				rapidjson::Value(rapidjson::kObjectType).Move(),
				m_impl->m_dom.GetAllocator()
			);
			m_impl->m_value = &(m_impl->top()->MemberEnd() - 1)->value;
		}
	}
	APT_VERIFY(enterObject());
}

void Json::beginArray(const char* _name)
{
	if (_name && find(_name)) {
		// object already existed, check the type
		APT_ASSERT(GetValueType(m_impl->m_value->GetType()) == ValueType_Array);
	}
	else {
		if (GetValueType(m_impl->top()->GetType()) == ValueType_Array) {
			if (_name) {
				APT_LOG("Json warning: calling beginArray() in an array, name '%s' will be ignored", _name);
			}
			m_impl->top()->PushBack(
				rapidjson::Value(rapidjson::kArrayType).Move(),
				m_impl->m_dom.GetAllocator()
			);
			m_impl->m_value = m_impl->top()->End() - 1;

		}
		else {
			m_impl->top()->AddMember(
				rapidjson::StringRef(_name),
				rapidjson::Value(rapidjson::kArrayType).Move(),
				m_impl->m_dom.GetAllocator()
			);
			m_impl->m_value = &(m_impl->top()->MemberEnd() - 1)->value;
		}
	}
	APT_VERIFY(enterArray());
}


template <> void Json::setValue<bool>(const char* _name, bool _val)
{
	if (find(_name)) {
		m_impl->m_value->SetBool(_val);
	}
	else {
		m_impl->top()->AddMember(
			rapidjson::StringRef(_name),
			rapidjson::Value(_val).Move(),
			m_impl->m_dom.GetAllocator()
		);
		m_impl->m_value = &(m_impl->top()->MemberEnd() - 1)->value;
	}
}
template <> void Json::setValue<bool>(int _i, bool _val)
{
	rapidjson::Value* top = m_impl->top();
	if (_i >= 0 && GetValueType(top->GetType()) == ValueType_Array) {
		m_impl->m_value->GetArray()[_i].SetBool(_val);
	}
	else {
		m_impl->m_value->SetBool(_val);
	}
}
template <> void Json::setValue<sint32>(const char* _name, sint32 _val)
{
	if (find(_name)) {
		m_impl->m_value->SetInt(_val);
	}
	else {
		m_impl->top()->AddMember(
			rapidjson::StringRef(_name),
			rapidjson::Value(_val).Move(),
			m_impl->m_dom.GetAllocator()
		);
		m_impl->m_value = &(m_impl->top()->MemberEnd() - 1)->value;
	}
}
template <> void Json::setValue<sint32>(int _i, sint32 _val)
{
	rapidjson::Value* top = m_impl->top();
	if (_i >= 0 && GetValueType(top->GetType()) == ValueType_Array) {
		m_impl->m_value->GetArray()[_i].SetInt(_val);
	}
	else {
		m_impl->m_value->SetInt(_val);
	}
}
template <> void Json::setValue<sint64>(const char* _name, sint64 _val)
{
	if (find(_name)) {
		m_impl->m_value->SetInt64(_val);
	}
	else {
		m_impl->top()->AddMember(
			rapidjson::StringRef(_name),
			rapidjson::Value(_val).Move(),
			m_impl->m_dom.GetAllocator()
		);
		m_impl->m_value = &(m_impl->top()->MemberEnd() - 1)->value;
	}
}
template <> void Json::setValue<sint64>(int _i, sint64 _val)
{
	rapidjson::Value* top = m_impl->top();
	if (_i >= 0 && GetValueType(top->GetType()) == ValueType_Array) {
		m_impl->m_value->GetArray()[_i].SetInt64(_val);
	}
	else {
		m_impl->m_value->SetInt64(_val);
	}
}
template <> void Json::setValue<sint16>(const char* _name, sint16 _val)
{
	setValue<sint32>(_name, (sint32)_val);
}
template <> void Json::setValue<sint16>(int _i, sint16 _val)
{
	setValue<sint32>(_i, (sint32)_val);
}
template <> void Json::setValue<sint8>(const char* _name, sint8 _val)
{
	setValue<sint32>(_name, (sint32)_val);
}
template <> void Json::setValue<sint8>(int _i, sint8 _val)
{
	setValue<sint32>(_i, (sint32)_val);
}
template <> void Json::setValue<uint64>(const char* _name, uint64 _val)
{
	if (find(_name)) {
		m_impl->m_value->SetUint64(_val);
	}
	else {
		m_impl->top()->AddMember(
			rapidjson::StringRef(_name),
			rapidjson::Value(_val).Move(),
			m_impl->m_dom.GetAllocator()
		);
		m_impl->m_value = &(m_impl->top()->MemberEnd() - 1)->value;
	}
}
template <> void Json::setValue<uint64>(int _i, uint64 _val)
{
	rapidjson::Value* top = m_impl->top();
	if (_i >= 0 && GetValueType(top->GetType()) == ValueType_Array) {
		m_impl->m_value->GetArray()[_i].SetUint64(_val);
	}
	else {
		m_impl->m_value->SetUint64(_val);
	}
}
template <> void Json::setValue<uint32>(const char* _name, uint32 _val)
{
	if (find(_name)) {
		m_impl->m_value->SetUint(_val);
	}
	else {
		m_impl->top()->AddMember(
			rapidjson::StringRef(_name),
			rapidjson::Value(_val).Move(),
			m_impl->m_dom.GetAllocator()
		);
		m_impl->m_value = &(m_impl->top()->MemberEnd() - 1)->value;
	}
}
template <> void Json::setValue<uint32>(int _i, uint32 _val)
{
	rapidjson::Value* top = m_impl->top();
	if (_i >= 0 && GetValueType(top->GetType()) == ValueType_Array) {
		m_impl->m_value->GetArray()[_i].SetUint(_val);
	}
	else {
		m_impl->m_value->SetUint(_val);
	}
}
template <> void Json::setValue<uint16>(const char* _name, uint16 _val)
{
	setValue<uint32>(_name, (uint16)_val);
}
template <> void Json::setValue<uint16>(int _i, uint16 _val)
{
	setValue<uint32>(_i, (uint16)_val);
}
template <> void Json::setValue<uint8>(const char* _name, uint8 _val)
{
	setValue<uint32>(_name, (uint32)_val);
}
template <> void Json::setValue<uint8>(int _i, uint8 _val)
{
	setValue<uint32>(_i, (uint32)_val);
}
template <> void Json::setValue<float32>(const char* _name, float32 _val)
{
	if (find(_name)) {
		m_impl->m_value->SetFloat(_val);
	}
	else {
		m_impl->top()->AddMember(
			rapidjson::StringRef(_name),
			rapidjson::Value(_val).Move(),
			m_impl->m_dom.GetAllocator()
		);
		m_impl->m_value = &(m_impl->top()->MemberEnd() - 1)->value;
	}
}
template <> void Json::setValue<float32>(int _i, float32 _val)
{
	rapidjson::Value* top = m_impl->top();
	if (_i >= 0 && GetValueType(top->GetType()) == ValueType_Array) {
		m_impl->m_value->GetArray()[_i].SetFloat(_val);
	}
	else {
		m_impl->m_value->SetFloat(_val);
	}
}
template <> void Json::setValue<float64>(const char* _name, float64 _val)
{
	if (find(_name)) {
		m_impl->m_value->SetDouble(_val);
	}
	else {
		m_impl->top()->AddMember(
			rapidjson::StringRef(_name),
			rapidjson::Value(_val).Move(),
			m_impl->m_dom.GetAllocator()
		);
		m_impl->m_value = &(m_impl->top()->MemberEnd() - 1)->value;
	}
}
template <> void Json::setValue<float64>(int _i, float64 _val)
{
	rapidjson::Value* top = m_impl->top();
	if (_i >= 0 && GetValueType(top->GetType()) == ValueType_Array) {
		m_impl->m_value->GetArray()[_i].SetDouble(_val);
	}
	else {
		m_impl->m_value->SetDouble(_val);
	}
}
template <> void Json::setValue<const char*>(const char* _name, const char* _val)
{
	if (find(_name)) {
		m_impl->m_value->SetString(_val, m_impl->m_dom.GetAllocator());
	}
	else {
		m_impl->top()->AddMember(
			rapidjson::StringRef(_name),
			rapidjson::Value().SetString(_val, m_impl->m_dom.GetAllocator()).Move(),
			m_impl->m_dom.GetAllocator()
		);
		m_impl->m_value = &(m_impl->top()->MemberEnd() - 1)->value;
	}
}
template <> void Json::setValue<const char*>(int _i, const char* _val)
{
	rapidjson::Value* top = m_impl->top();
	if (_i >= 0 && GetValueType(top->GetType()) == ValueType_Array) {
		m_impl->m_value->GetArray()[_i].SetString(_val, m_impl->m_dom.GetAllocator());
	}
	else {
		m_impl->m_value->SetString(_val, m_impl->m_dom.GetAllocator());
	}
}

template <> void Json::pushValue<bool>(bool _val)
{
	m_impl->top()->PushBack(
		rapidjson::Value(_val).Move(),
		m_impl->m_dom.GetAllocator()
	);
	m_impl->m_value = m_impl->top()->End() - 1;
}

template <> void Json::pushValue<sint64>(sint64 _val)
{
	m_impl->top()->PushBack(
		rapidjson::Value(_val).Move(),
		m_impl->m_dom.GetAllocator()
	);
	m_impl->m_value = m_impl->top()->End() - 1;
}
template <> void Json::pushValue<sint32>(sint32 _val)
{
	m_impl->top()->PushBack(
		rapidjson::Value(_val).Move(),
		m_impl->m_dom.GetAllocator()
	);
	m_impl->m_value = m_impl->top()->End() - 1;
}
template <> void Json::pushValue<sint16>(sint16 _val)
{
	pushValue<sint32>((sint32)_val);
}
template <> void Json::pushValue<sint8>(sint8 _val)
{
	pushValue<sint32>((sint32)_val);
}
template <> void Json::pushValue<uint64>(uint64 _val)
{
	m_impl->top()->PushBack(
		rapidjson::Value(_val).Move(),
		m_impl->m_dom.GetAllocator()
	);
	m_impl->m_value = m_impl->top()->End() - 1;
}
template <> void Json::pushValue<uint32>(uint32 _val)
{
	m_impl->top()->PushBack(
		rapidjson::Value(_val).Move(),
		m_impl->m_dom.GetAllocator()
	);
	m_impl->m_value = m_impl->top()->End() - 1;
}
template <> void Json::pushValue<uint16>(uint16 _val)
{
	pushValue((uint32)_val);
}
template <> void Json::pushValue<uint8>(uint8 _val)
{
	pushValue((uint32)_val);
}
template <> void Json::pushValue<float32>(float32 _val)
{
	m_impl->top()->PushBack(
		rapidjson::Value(_val).Move(),
		m_impl->m_dom.GetAllocator()
	);
	m_impl->m_value = m_impl->top()->End() - 1;
}
template <> void Json::pushValue<float64>(float64 _val)
{
	m_impl->top()->PushBack(
		rapidjson::Value(_val).Move(),
		m_impl->m_dom.GetAllocator()
	);
	m_impl->m_value = m_impl->top()->End() - 1;
}
template <> void Json::pushValue<const char*>(const char* _val)
{
	m_impl->top()->PushBack(
		rapidjson::Value().SetString(_val, m_impl->m_dom.GetAllocator()).Move(),
		m_impl->m_dom.GetAllocator()
	);
	m_impl->m_value = m_impl->top()->End() - 1;
}


template <> void Json::setValue<vec2>(const char* _name, vec2 _val)
{
	beginArray(_name);
	pushValue(_val.x);
	pushValue(_val.y);
	leaveArray();
}
template <> void Json::setValue<vec3>(const char* _name, vec3 _val)
{
	beginArray(_name);
	pushValue(_val.x);
	pushValue(_val.y);
	pushValue(_val.z);
	leaveArray();
}
template <> void Json::setValue<vec4>(const char* _name, vec4 _val)
{
	beginArray(_name);
	pushValue(_val.x);
	pushValue(_val.y);
	pushValue(_val.z);
	pushValue(_val.w);
	leaveArray();
}
template <> void Json::pushValue<vec2>(vec2 _val)
{
	beginArray();
	pushValue(_val.x);
	pushValue(_val.y);
	leaveArray();
}
template <> void Json::pushValue<vec3>(vec3 _val)
{
	beginArray();
	pushValue(_val.x);
	pushValue(_val.y);
	pushValue(_val.z);
	leaveArray();
}
template <> void Json::pushValue<vec4>(vec4 _val)
{
	beginArray();
	pushValue(_val.x);
	pushValue(_val.y);
	pushValue(_val.z);
	pushValue(_val.w);
	leaveArray();
}

template <> void Json::setValue<mat2>(const char* _name, mat2 _val)
{
	beginArray(_name);
	for (int i = 0; i < 2; ++i) {
		pushValue<vec2>(_val[i]);
	}
	leaveArray();
}
template <> void Json::setValue<mat3>(const char* _name, mat3 _val)
{
	beginArray(_name);
	for (int i = 0; i < 3; ++i) {
		pushValue<vec3>(_val[i]);
	}
	leaveArray();
}
template <> void Json::setValue<mat4>(const char* _name, mat4 _val)
{
	beginArray(_name);
	for (int i = 0; i < 4; ++i) {
		pushValue<vec4>(_val[i]);
	}
	leaveArray();
}
template <> void Json::pushValue<mat2>(mat2 _val)
{
	beginArray();
	for (int i = 0; i < 2; ++i) {
		pushValue<vec2>(_val[i]);
	}
	leaveArray();
}
template <> void Json::pushValue<mat3>(mat3 _val)
{
	beginArray();
	for (int i = 0; i < 3; ++i) {
		pushValue<vec3>(_val[i]);
	}
	leaveArray();
}
template <> void Json::pushValue<mat4>(mat4 _val)
{
	beginArray();
	for (int i = 0; i < 4; ++i) {
		pushValue<vec4>(_val[i]);
	}
	leaveArray();
}

/*******************************************************************************

                              JsonSerializer

*******************************************************************************/

JsonSerializer::JsonSerializer(Json* _json_, Mode _mode)
	: m_json(_json_)
	, m_mode(_mode)
{
}

bool JsonSerializer::beginObject(const char* _name)
{
	if (m_mode == Mode_Read) {
		if (insideArray()) {
			if (!m_json->next()) {
				return false;
			}
		} else {
			APT_ASSERT(_name);
			if (!m_json->find(_name)) {
				return false;
			}
		}

		if (m_json->getType() == Json::ValueType_Object) {
			m_json->enterObject();
			return true;
		}

	} else {
		m_json->beginObject(_name);
		return true;
	}
	return false;
}
void JsonSerializer::endObject()
{
	if (m_mode == Mode_Read) {
		m_json->leaveObject();
	} else {
		m_json->endObject();
	}
}

bool JsonSerializer::beginArray(const char* _name)
{
	if (m_mode == Mode_Read) {
		if (insideArray()) {
			if (!m_json->next()) {
				return false;
			}
		} else {
			APT_ASSERT(_name);
			if (!m_json->find(_name)) {
				return false;
			}
		}
				
		if (m_json->getType() == Json::ValueType_Array) {
			m_json->enterArray();
			return true;
		}

	} else {
		m_json->beginArray(_name);
		return true;
	}
	return false;
}
void JsonSerializer::endArray()
{
	if (m_mode == Mode_Read) {
		m_json->leaveArray();
	} else {
		m_json->endArray();
	}
}

#define DEFINE_value(_type) \
	template <> bool JsonSerializer::value<_type>(const char* _name, _type& _value_) { \
		APT_ASSERT_MSG(!insideArray(), "JsonSerializer::value: _name variant called inside an array"); \
		if (m_mode == Mode_Read) { \
			if (m_json->find(_name)) { \
				_value_ = m_json->getValue<_type>(); \
				return true; \
			} \
		} else { \
			m_json->setValue<_type>(_name, _value_); \
			return true; \
		} \
		return false; \
	} \
	template <> bool JsonSerializer::value<_type>(_type& _value_) { \
		APT_ASSERT_MSG(insideArray(), "JsonSerializer::value: array variant called outside an array"); \
		if (m_mode == Mode_Read) { \
			if (!m_json->next()) { \
				return false; \
			} \
			_value_ = m_json->getValue<_type>(); \
		} else { \
			m_json->pushValue<_type>(_value_); \
		} \
		return true; \
	}

DEFINE_value(bool)
DEFINE_value(sint8)
DEFINE_value(sint32)
DEFINE_value(sint64)
DEFINE_value(uint8)
DEFINE_value(uint32)
DEFINE_value(uint64)
DEFINE_value(float32)
DEFINE_value(float64)
DEFINE_value(vec2)
DEFINE_value(vec3)
DEFINE_value(vec4)
DEFINE_value(mat2)
DEFINE_value(mat3)
DEFINE_value(mat4)

#undef DEFINE_value

template <> bool JsonSerializer::value<StringBase>(const char* _name, StringBase& _value_)
{
	APT_ASSERT(!insideArray());
	if (m_mode == Mode_Read) {
		int ln = string(_name, 0);
		_value_.setCapacity(ln + 1);
	}
	return string(_name, _value_) != 0;
}

template <> bool JsonSerializer::value<StringBase>(StringBase& _value_)
{
	APT_ASSERT(insideArray());
	if (m_mode == Mode_Read) {
		int ln = string(0);
		if (ln == 0) {
			return false;
		}
		_value_.setCapacity(ln + 1);
	}
	return string(_value_) != 0;
}

int JsonSerializer::string(const char* _name, char* _string_)
{
	APT_ASSERT(!insideArray()); 
	if (m_mode == Mode_Read) {
		if (m_json->find(_name)) {
			if (_string_) { 
			 // valid ptr, copy string to buffer
				const char* str = m_json->getValue<const char*>();
				strcpy(_string_, str);
				return (int)strlen(str);
			} else {
				return (int)strlen(m_json->getValue<const char*>());
			}
		}
	} else {
		APT_ASSERT(_string_);
		m_json->setValue(_name, (const char*)_string_);
		return (int)strlen(_string_);
	}
	return 0;
}

int JsonSerializer::string(char* _value_)
{
	APT_ASSERT(insideArray()); 
	if (m_mode == Mode_Read) {
		if (_value_) {
		 // valid ptr, copy string to buffer
			const char* str = m_json->getValue<const char*>();
			strcpy(_value_, str);
			return (int)strlen(str);
		} else {
			if (!m_json->next()) {
				return 0;
			}
			return (int)strlen(m_json->getValue<const char*>());
		}
	} else {
		APT_ASSERT(_value_);
		m_json->pushValue((const char*)_value_);
		return (int)strlen(_value_);
	}
	return 0;
}

// PRIVATE

bool JsonSerializer::insideArray()
{
	rapidjson::Value* top = m_json->m_impl->top();
	if (top) {
		return GetValueType(top->GetType()) == Json::ValueType_Array;
	} else {
		return false;
	}
}
