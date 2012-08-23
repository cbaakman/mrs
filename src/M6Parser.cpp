#include "M6Lib.h"

#include <iostream>

#include <boost/filesystem/operations.hpp>

#include "M6Parser.h"
#include "M6Error.h"
#include "M6Config.h"

using namespace std;
namespace fs = boost::filesystem;

// --------------------------------------------------------------------

void M6Parser::ParseDocument(M6InputDocument* inDoc)
{
	mDoc = inDoc;
}

//// --------------------------------------------------------------------
//
//M6XMLScriptParser::M6XMLScriptParser(zeep::xml::element* inScript)
//{
//}
//
//void M6XMLScriptParser::ParseDocument(M6InputDocument* inDoc)
//{
//	M6Parser::ParseDocument(inDoc);
//	// ...
//}

// --------------------------------------------------------------------
// Perl based parser implementation

#include <Windows.h>
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#undef INT64_C	// 
#undef bind
#undef bool

// ------------------------------------------------------------------

extern "C"
{
void xs_init(pTHX);
void boot_DynaLoader(pTHX_ CV* cv);
void boot_Win32CORE(pTHX_ CV* cv);
}

// ------------------------------------------------------------------

struct M6PerlParserImpl
{
						M6PerlParserImpl(const string& inScriptName, const fs::path& inScriptDir);
						~M6PerlParserImpl();

	void				Parse(M6InputDocument* inDoc);
	void				GetVersion(string& outVersion);
	
	// implemented callbacks
	void				IndexText(const string& inIndex, const char* inText, size_t inLength)
							{ mDocument->Index(inIndex, eM6TextData, false, inText, inLength); }
	void				IndexString(const string& inIndex, const char* inText, size_t inLength, bool inUnique)
							{ mDocument->Index(inIndex, eM6StringData, inUnique, inText, inLength); }
	void				IndexDate(const string& inIndex, const char* inText, size_t inLength)
							{ mDocument->Index(inIndex, eM6DateData, false, inText, inLength); }
	void				IndexNumber(const string& inIndex, const char* inText, size_t inLength)
							{ mDocument->Index(inIndex, eM6NumberData, false, inText, inLength); }

	void				AddLink(const string& inDatabank, const string& inValue)
							{ mDocument->AddLink(inDatabank, inValue); }
	void				SetAttribute(const string& inField, const char* inValue, size_t inLength)
							{ mDocument->SetAttribute(inField, inValue, inLength); }

	// Perl interface routines
	static const char*	kM6ScriptType;
	static const char*	kM6ScriptObjectName;

	static M6PerlParserImpl*
						GetObject(SV* inScalar);

	HV*					GetHash()				{ return mParserHash; }
	
	string				operator[](const char* inEntry);
	static string		GetString(SV* inScalar);
	
	PerlInterpreter*	mPerl;
	SV*					mParser;
	HV*					mParserHash;
	static M6PerlParserImpl* sConstructingImp;

	M6InputDocument*	mDocument;
};

const char* M6PerlParserImpl::kM6ScriptType = "M6::Script";
const char* M6PerlParserImpl::kM6ScriptObjectName = "M6::Script::Object";
M6PerlParserImpl* M6PerlParserImpl::sConstructingImp = nullptr;

M6PerlParserImpl::M6PerlParserImpl(const string& inScriptName, const fs::path& inScriptDir)
	: mPerl(nullptr)
	, mParser(nullptr)
	, mParserHash(nullptr)
{
	static bool sInited = false;
	if (not sInited)
	{
		int argc = 0;
		const char* env[] = { nullptr };
		const char* argv[] = { nullptr };

		PERL_SYS_INIT3(&argc, (char***)&argv, (char***)&env);
		sInited = true;
	}

	mPerl = perl_alloc();
	if (mPerl == nullptr)
		THROW(("error allocating perl interpreter"));
	
	perl_construct(mPerl);
	
	PL_origalen = 1;
	
	PERL_SET_CONTEXT(mPerl);
		
	fs::path baseParser = inScriptDir / "M6Script.pm";

	if (not fs::exists(baseParser))
		THROW(("The M6Script.pm script could not be found in %s", inScriptDir.c_str()));
	
	string baseParserPath = baseParser.string();
	const char* embedding[] = { "", baseParserPath.c_str() };
	
	mParserHash = newHV();
	
	// init the parser hash
//	(void)hv_store_ent(mParserHash,
//		newSVpv("db", 2),
//		newSVpv(inDatabank.c_str(), inDatabank.length()), 0);
//
//	string path = inRawDir.string();
//	(void)hv_store_ent(mParserHash,
//		newSVpv("raw_dir", 7),
//		newSVpv(path.c_str(), path.length()), 0);

	string path = inScriptDir.string();
	(void)hv_store_ent(mParserHash,
		newSVpv("script_dir", 7),
		newSVpv(path.c_str(), path.length()), 0);
	
	int err = perl_parse(mPerl, xs_init, 2, const_cast<char**>(embedding), nullptr);
	SV* errgv = GvSV(PL_errgv);

	if (err != 0)
	{
		string errmsg = "no perl error available";
	
		if (SvPOK(errgv))
			errmsg = string(SvPVX(errgv), SvCUR(errgv));
		
		THROW(("Error parsing M6Script.pm module: %s", errmsg.c_str()));
	}
	
	dSP;
	ENTER;
	SAVETMPS;
	PUSHMARK(SP);
	
	path = inScriptDir.string();
	XPUSHs(sv_2mortal(newSVpv(path.c_str(), path.length())));
	XPUSHs(sv_2mortal(newSVpv(inScriptName.c_str(), inScriptName.length())));

	PUTBACK;
	
	int n;
	static boost::mutex sGlobalGuard;
	{
		boost::mutex::scoped_lock lock(sGlobalGuard);
		
		sConstructingImp = this;
		n = call_pv("M6::load_script", G_SCALAR | G_EVAL);
		sConstructingImp = nullptr;
	}

	SPAGAIN;

	if (n != 1 or SvTRUE(ERRSV))
	{
		string errmsg(SvPVX(ERRSV), SvCUR(ERRSV));
		THROW(("Perl error creating script object: %s", errmsg.c_str()));
	}

	mParser = newRV(POPs);
	
	PUTBACK;
	FREETMPS;
	LEAVE;
}

M6PerlParserImpl::~M6PerlParserImpl()
{
	PL_perl_destruct_level = 0;
	perl_destruct(mPerl);
	perl_free(mPerl);
}

void M6PerlParserImpl::Parse(M6InputDocument* inDocument)
{
	mDocument = inDocument;
	
//	// load index info, if not done already
//	if (mCaseSensitiveIndices.empty())
//	{
//		SV** sv = hv_fetch(mParserHash, "indices", 7, 0);
//		if (sv != nullptr)
//		{
//			HV* hv = nullptr;
//		
//			if (SvTYPE(*sv) == SVt_PVHV)
//				hv = (HV*)*sv;
//			else if (SvROK(*sv))
//			{
//				SV* rv = SvRV(*sv);
//				if (SvTYPE(rv) == SVt_PVHV)
//					hv = (HV*)rv;
//			}
//			
//			if (hv != nullptr)
//			{
//				uint32 n = hv_iterinit(hv);
//				
//				while (n-- > 0)
//				{
//					STRLEN len;
//					
//					HE* he = hv_iternext(hv);
//					
//					if (he == nullptr)
//						break;
//					
//					string id = HePV(he, len);
//					
//					SV* v = HeVAL(he);
//					if (v != nullptr)
//					{
//						if (SvRV(v))
//							v = SvRV(v);
//						
//						if (SvTYPE(v) == SVt_PVHV)
//						{
//							HV* hash = (HV*)v;
//							
//							sv = hv_fetch(hash, "casesensitive", 13, 0);
//							if (sv != nullptr and SvIOK(*sv) and SvIVX(*sv) != 0)
//								mCaseSensitiveIndices.insert(id);
//						}
//					}
//				}
//			}
//		}
//	}
	
	const string& text = mDocument->Peek();

	PERL_SET_CONTEXT(mPerl);

	dSP;
	ENTER;
	SAVETMPS;
	PUSHMARK(SP);
	
	XPUSHs(SvRV(mParser));
	XPUSHs(sv_2mortal(newSVpv(text.c_str(), text.length())));

	PUTBACK;
	
	call_method("parse", G_SCALAR | G_EVAL);

	SPAGAIN;

	string errmsg;
	if (SvTRUE(ERRSV))
		errmsg.assign(SvPVX(ERRSV), SvCUR(ERRSV));

	PUTBACK;
	FREETMPS;
	LEAVE;

	if (not errmsg.empty())
	{
		cerr << endl
			 << "Error parsing document: " << endl
			 << errmsg << endl;
		exit(1);
	}
		
	mDocument = nullptr;
}

//void M6PerlParserImpl::GetVersion(
//	string&				outVersion)
//{
//	PERL_SET_CONTEXT(mPerl);
//
//	dSP;
//	ENTER;
//	SAVETMPS;
//	PUSHMARK(SP);
//	
//	XPUSHs(SvRV(mParser));
//
//	PUTBACK;
//	
//	int n = call_method("version", G_EVAL);
//
//	SPAGAIN;
//	
//	string errmsg;
//
//	if (SvTRUE(ERRSV))
//		errmsg = string(SvPVX(ERRSV), SvCUR(ERRSV));
//	else if (n == 1 and SvPOK(*SP))
//		outVersion = SvPVX(POPs);
//
//	PUTBACK;
//	FREETMPS;
//	LEAVE;
//	
//	if (errmsg.length())
//		THROW(("Error calling version: %s", errmsg.c_str()));
//
//	if (n != 1 or outVersion.empty())
//		THROW(("version method of parser script should return one version string"));
//	
//	mCallback.clear();
//	mUserData = nullptr;
//}

M6PerlParserImpl* M6PerlParserImpl::GetObject(
	SV*					inScalar)
{
	M6PerlParserImpl* result = nullptr;
	
	if (SvGMAGICAL(inScalar))
		mg_get(inScalar);
	
	if (sv_isobject(inScalar))
	{
		SV* tsv = SvRV(inScalar);

		IV tmp = 0;

		if (SvTYPE(tsv) == SVt_PVHV)
		{
			if (SvMAGICAL(tsv))
			{
				MAGIC* mg = mg_find(tsv, 'P');
				if (mg != nullptr)
				{
					inScalar = mg->mg_obj;
					if (sv_isobject(inScalar))
						tmp = SvIV(SvRV(inScalar));
				}
			}
		}
		else
			tmp = SvIV(SvRV(inScalar));
		
		if (tmp != 0 and strcmp(kM6ScriptType, HvNAME(SvSTASH(SvRV(inScalar)))) == 0)
			result = reinterpret_cast<M6PerlParserImpl*>(tmp);
	}
	
	return result;
}

// ------------------------------------------------------------------

string M6PerlParserImpl::operator[](const char* inEntry)
{
	string result;
	
	HV* hash = GetHash();

	if (hash == nullptr)
		THROW(("runtime error"));
	
	uint32 len = static_cast<uint32>(strlen(inEntry));
	
	SV** sv = hv_fetch(hash, inEntry, len, 0);
	if (sv != nullptr)
		result = GetString(*sv);
	
	return result;
}

string M6PerlParserImpl::GetString(SV* inScalar)
{
	string result;
	
	if (inScalar != nullptr)
	{
		if (SvPOK(inScalar))
			result = SvPVX(inScalar);
		else
		{
			STRLEN len;
			result = SvPV(inScalar, len);
		}
	}
	
	return result;
}

// ------------------------------------------------------------------

XS(_M6_Script_new)
{
	dXSARGS;
	
	SV* obj = newSV(0);
	HV* hash = newHV();
	
	sv_setref_pv(obj, "M6::Script", M6PerlParserImpl::sConstructingImp);
	HV* stash = SvSTASH(SvRV(obj));
	
	GV* gv = *(GV**)hv_fetch(stash, "OWNER", 5, true);
	if (not isGV(gv))
		gv_init(gv, stash, "OWNER", 5, false);
	
	HV* hv = GvHVn(gv);
	hv_store_ent(hv, obj, newSViv(1), 0);
	
	sv_magic((SV*)hash, (SV*)obj, 'P', Nullch, 0);
	sv_free(obj);

	SV* sv = sv_newmortal();

	SV* self = newRV_noinc((SV*)hash);
	sv_setsv_flags(sv, self, SV_GMAGIC);
	sv_free((SV*)self);
	sv_bless(sv, stash);
	
	// copy the hash values into our hash
	
	for (int i = 0; i < items; i += 2)
	{
		SV* key = ST(i);
		SV* value = ST(i + 1);

		SvREFCNT_inc(value);
		
		HE* e = hv_store_ent(M6PerlParserImpl::sConstructingImp->GetHash(), key, value, 0);
		if (e == nullptr)
			sv_free(value);
	}
	
	ST(0) = sv;
	
	XSRETURN(1);
}

XS(_M6_Script_DESTROY)
{
	dXSARGS;
	
	if (items != 1)
		croak("Usage: M6::Script::DESTROY(self);");

	// the parser was deleted

	XSRETURN(0);
}

XS(_M6_Script_FIRSTKEY)
{
	dXSARGS;

	if (items != 1)
		croak("Usage: M6::Script::FIRSTKEY(self);");

	M6PerlParserImpl* proxy = M6PerlParserImpl::GetObject(ST(0));
	
	if (proxy == nullptr)
		croak("Error, M6::Script object is not specified");

	(void)hv_iterinit(proxy->GetHash());
	HE* e = hv_iternext(proxy->GetHash());
	if (e == nullptr)
	{
		ST(0) = sv_newmortal();
		sv_setsv_flags(ST(0), &PL_sv_undef, SV_GMAGIC);
	}
	else
		ST(0) = hv_iterkeysv(e);

	XSRETURN(1);
}

XS(_M6_Script_NEXTKEY)
{
	dXSARGS;
	
	if (items < 1)
		croak("Usage: M6::Script::NEXTKEY(self);");

	M6PerlParserImpl* proxy = M6PerlParserImpl::GetObject(ST(0));
	
	if (proxy == nullptr)
		croak("Error, M6::Script object is not specified");

	HE* e = hv_iternext(proxy->GetHash());
	if (e == nullptr)
	{
		ST(0) = sv_newmortal();
		sv_setsv_flags(ST(0), &PL_sv_undef, SV_GMAGIC);
	}
	else
		ST(0) = hv_iterkeysv(e);

	XSRETURN(1);
}

XS(_M6_Script_FETCH)
{
	dXSARGS;

	if (items != 2)
		croak("Usage: M6::Script::FETCH(self, name);");

	M6PerlParserImpl* proxy = M6PerlParserImpl::GetObject(ST(0));
	
	if (proxy == nullptr)
		croak("Error, M6::Script object is not specified");

	SV* key = ST(1);

	HE* e = hv_fetch_ent(proxy->GetHash(), key, 0, 0);
	
	SV* result;
	
	if (e != nullptr)
	{
		result = HeVAL(e);
		SvREFCNT_inc(result);
	}
	else
	{
		result = sv_newmortal();
		sv_setsv_flags(result, &PL_sv_undef, SV_GMAGIC);
	}
	
	ST(0) = result;
	XSRETURN(1);
}

XS(_M6_Script_STORE)
{
	dXSARGS;

	if (items != 3)
		croak("Usage: M6::Script::STORE(self, name, value);");

	M6PerlParserImpl* proxy = M6PerlParserImpl::GetObject(ST(0));
	
	if (proxy == nullptr)
		croak("Error, M6::Script object is not specified");

	SV* key = ST(1);
	SV* value = ST(2);
	
	SvREFCNT_inc(value);
	
	HE* e = hv_store_ent(proxy->GetHash(), key, value, 0);
	if (e == nullptr)
		sv_free(value);

	XSRETURN(0);
}

XS(_M6_Script_CLEAR)
{
	dXSARGS;
	
	if (items < 1)
		croak("Usage: M6::Script::CLEAR(self);");

	M6PerlParserImpl* proxy = M6PerlParserImpl::GetObject(ST(0));
	
	if (proxy == nullptr)
		croak("Error, M6::Script object is not specified");
	
	hv_clear(proxy->GetHash());

	XSRETURN(0);
}

XS(_M6_Script_set_attribute)
{
	dXSARGS;
	
	if (items != 3)
		croak("Usage: M6::Script::set_attribute(self, field, text);");
	
	M6PerlParserImpl* proxy = M6PerlParserImpl::GetObject(ST(0));
	if (proxy == nullptr)
		croak("Error, M6::Script object is not specified");
	
	const char* ptr;
	STRLEN len;
	
	ptr = SvPV(ST(1), len);
	if (ptr == nullptr)
		croak("Error, no field defined in call to set_attribute");
	
	string name(ptr, len);

	ptr = SvPV(ST(2), len);
	if (ptr == nullptr)
		croak("Error, no text defined in call to set_attribute");
	
	try
	{
		proxy->SetAttribute(name, ptr, len);
	}
	catch (exception& e)
	{
		croak(e.what());
	}
	
	XSRETURN(0);
}

XS(_M6_Script_index_text)
{
	dXSARGS;
	
	if (items != 3)
		croak("Usage: M6::Script::index_text(self, indexname, text);");
	
	M6PerlParserImpl* proxy = M6PerlParserImpl::GetObject(ST(0));
	if (proxy == nullptr)
		croak("Error, M6::Script object is not specified");

	// fetch the parameters
	
	string index;
	const char* ptr;
	STRLEN len;
	
	ptr = SvPV(ST(1), len);
	if (ptr == nullptr or len == 0)
		croak("Error, indexname is undefined in call to index_text");

	index.assign(ptr, len);
	
	ptr = SvPV(ST(2), len);
	if (ptr != nullptr and len > 0)
	{
		try
		{
			proxy->IndexText(index, ptr, len);
		}
		catch (exception& e)
		{
			croak(e.what());
		}
	}
//	else if (VERBOSE)
//		cout << "Warning, text is undefined in call to IndexText" << endl;
	
	XSRETURN(0);
}

XS(_M6_Script_index_string)
{
	dXSARGS;
	
	if (items != 3)
		croak("Usage: M6::Script::index_string(self, indexname, str);");
	
	M6PerlParserImpl* proxy = M6PerlParserImpl::GetObject(ST(0));
	if (proxy == nullptr)
		croak("Error, M6::Script object is not specified");
	
	// fetch the parameters
	
	const char* ptr;
	STRLEN len;
	
	ptr = SvPV(ST(1), len);
	if (ptr == nullptr or len == 0)
		croak("Error, indexname is undefined in call to index_string");

	string index(ptr, len);
	
	ptr = SvPV(ST(2), len);
	if (ptr != nullptr and len > 0)
	{
		try
		{
			proxy->IndexString(index, ptr, len, false);
		}
		catch (exception& e)
		{
			croak(e.what());
		}
	}
	
	XSRETURN(0);
}

XS(_M6_Script_index_unique_string)
{
	dXSARGS;
	
	if (items != 3)
		croak("Usage: M6::Script::index_unique_string(self, indexname, str);");
	
	M6PerlParserImpl* proxy = M6PerlParserImpl::GetObject(ST(0));
	if (proxy == nullptr)
		croak("Error, M6::Script object is not specified");
	
	// fetch the parameters
	
	const char* ptr;
	STRLEN len;
	
	ptr = SvPV(ST(1), len);
	if (ptr == nullptr or len == 0)
		croak("Error, indexname is undefined in call to index_unique_string");

	string index(ptr, len);
	
	ptr = SvPV(ST(2), len);
	if (ptr != nullptr and len > 0)
	{
		try
		{
			proxy->IndexString(index, ptr, len, true);
		}
		catch (exception& e)
		{
			croak(e.what());
		}
	}
	
	XSRETURN(0);
}

XS(_M6_Script_index_number)
{
	dXSARGS;
	
	if (items != 3)
		croak("Usage: M6::Script::index_number(self, indexname, value);");
	
	M6PerlParserImpl* proxy = M6PerlParserImpl::GetObject(ST(0));
	if (proxy == nullptr)
		croak("Error, M6::Script object is not specified");
	
	// fetch the parameters
	
	string index, value;
	const char* ptr;
	STRLEN len;
	
	ptr = SvPV(ST(1), len);
	if (ptr == nullptr or len == 0)
		croak("Error, indexname is undefined in call to index_number");

	index.assign(ptr, len);
	
	ptr = SvPV(ST(2), len);
	if (ptr == nullptr or len == 0)
		croak("Error, value is undefined in call to IndexValue");

	try
	{
		proxy->IndexNumber(index, ptr, len);
	}
	catch (exception& e)
	{
		croak(e.what());
	}
	
	XSRETURN(0);
}

XS(_M6_Script_index_date)
{
	dXSARGS;
	
	if (items != 3)
		croak("Usage: M6::Script::index_date(self, indexname, date);");
	
	M6PerlParserImpl* proxy = M6PerlParserImpl::GetObject(ST(0));
	if (proxy == nullptr)
		croak("Error, M6::Script object is not specified");
	
	// fetch the parameters
	
	const char* ptr;
	STRLEN len;
	
	ptr = SvPV(ST(1), len);
	if (ptr == nullptr or len == 0)
		croak("Error, indexname is undefined in call to index_date");

	string index(ptr, len);
	
	ptr = SvPV(ST(2), len);
	if (ptr == nullptr or len == 0)
		croak("Error, value is undefined in call to index_date");

	try
	{
		proxy->IndexDate(index, ptr, len);
	}
	catch (exception& e)
	{
		croak(e.what());
	}
	
	XSRETURN(0);
}

XS(_M6_Script_add_link)
{
	dXSARGS;
	
	if (items != 3)
		croak("Usage: M6::Script::add_link(self, databank, value);");
	
	M6PerlParserImpl* proxy = M6PerlParserImpl::GetObject(ST(0));
	if (proxy == nullptr)
		croak("Error, M6::Script object is not specified");
	
	// fetch the parameters
	
	const char* ptr;
	STRLEN len;
	
	ptr = SvPV(ST(1), len);
	if (ptr == nullptr or len == 0)
		croak("Error, databank is undefined in call to add_link");

	string databank(ptr, len);
	
	ptr = SvPV(ST(2), len);
	if (ptr == nullptr or len == 0)
		croak("Error, value is undefined in call to add_link");

	string value(ptr, len);
	
	try
	{
		proxy->AddLink(databank, value);
	}
	catch (exception& e)
	{
		croak(e.what());
	}
	
	XSRETURN(0);
}

void xs_init(pTHX)
{
	char *file = const_cast<char*>(__FILE__);
	dXSUB_SYS;

	/* DynaLoader is a special case */
	newXS(const_cast<char*>("DynaLoader::boot_DynaLoader"), boot_DynaLoader, file);

#if defined(_MSC_VER)
	newXS("Win32CORE::bootstrap", boot_Win32CORE, file);
#endif

	// our methods
	newXS(const_cast<char*>("M6::new_M6_Script"), _M6_Script_new, file);
	newXS(const_cast<char*>("M6::delete_M6_Script"), _M6_Script_DESTROY, file);

	newXS(const_cast<char*>("M6::Script::FIRSTKEY"), _M6_Script_FIRSTKEY, file);
	newXS(const_cast<char*>("M6::Script::NEXTKEY"), _M6_Script_NEXTKEY, file);
	newXS(const_cast<char*>("M6::Script::FETCH"), _M6_Script_FETCH, file);
	newXS(const_cast<char*>("M6::Script::STORE"), _M6_Script_STORE, file);
	newXS(const_cast<char*>("M6::Script::CLEAR"), _M6_Script_CLEAR, file);

	newXS(const_cast<char*>("M6::Script::set_attribute"), _M6_Script_set_attribute, file);
	newXS(const_cast<char*>("M6::Script::index_text"), _M6_Script_index_text, file);
	newXS(const_cast<char*>("M6::Script::index_string"), _M6_Script_index_string, file);
	newXS(const_cast<char*>("M6::Script::index_unique_string"), _M6_Script_index_unique_string, file);
	newXS(const_cast<char*>("M6::Script::index_number"), _M6_Script_index_number, file);
	newXS(const_cast<char*>("M6::Script::index_date"), _M6_Script_index_date, file);
	
	newXS(const_cast<char*>("M6::Script::add_link"), _M6_Script_add_link, file);

//	// a couple of constants
//	sv_setiv(get_sv("M6::IS_VALUE_INDEX", true),	eIsValue);
//	sv_setiv(get_sv("M6::INDEX_NUMBERS", true),	eIndexNumbers);
//	sv_setiv(get_sv("M6::STORE_AS_META", true),	eStoreAsMetaData);
//	sv_setiv(get_sv("M6::STORE_IDL", true),		eStoreIDL);
//	sv_setiv(get_sv("M6::INDEX_STRING", true),		eIndexString);
}

M6PerlParser::M6PerlParser(const string& inName)
	: mName(inName)
{
}

void M6PerlParser::ParseDocument(M6InputDocument* inDoc)
{
	fs::path scriptdir = M6Config::Instance().FindGlobal("/m6-config/scriptdir");
	
	if (not fs::exists(scriptdir) and fs::exists("./parsers"))
		scriptdir = fs::path("./parsers");
	else
		THROW(("scriptdir not found or incorrectly specified"));
	
	if (mImpl.get() == nullptr)
		mImpl.reset(new M6PerlParserImpl(mName, scriptdir));
	
	M6Parser::ParseDocument(inDoc);
	mImpl->Parse(inDoc);
}

