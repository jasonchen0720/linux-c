#ifndef __UAPI_JSON_H__
#define __UAPI_JSON_H__
#define JSON_key(key)		"\""#key"\":"


#define KF_d(key)		JSON_key(key)"%d"
#define KF_u(key)		JSON_key(key)"%u"
#define KF_ld(key)		JSON_key(key)"%ld"
#define KF_lu(key)		JSON_key(key)"%lu"
#define KF_lld(key)		JSON_key(key)"%lld"
#define KF_llu(key)		JSON_key(key)"%llu"
#define KF_s(key)		JSON_key(key)"\"%s\""
#define KF_a(key, J)	JSON_key(key)"["J"]"
#define KF_o(key, J)	JSON_key(key)""J

#define JSON_null()	\
	""
	
#define JSON_fmt0()	\
	"{}"

#define JSON_fmt1(K)\
	"{"K"}"

#define JSON_fmt2(K1, K2)\
	"{"K1","K2"}"

#define JSON_fmt3(K1, K2, K3)\
	"{"K1","K2","K3"}"

#define JSON_fmt4(K1, K2, K3,  K4)\
	"{"K1","K2","K3","K4"}"

#define JSON_fmt5(K1, K2, K3,  K4, K5)\
	"{"K1","K2","K3","K4","K5"}"

#define JSON_fmt6(K1, K2, K3,  K4, K5, K6)\
	"{"K1","K2","K3","K4","K5","K6"}"

#define JSON_fmt7(K1, K2, K3,  K4, K5, K6, K7)\
	"{"K1","K2","K3","K4","K5","K6","K7"}"

#define JSON_fmt8(K1, K2, K3, K4, K5, K6, K7, K8)\
	"{"K1","K2","K3","K4","K5","K6","K7","K8"}"

#define JSON_fmt9(K1, K2, K3, K4, K5, K6, K7, K8, K9)\
	"{"K1","K2","K3","K4","K5","K6","K7","K8","K9"}"

#define JSON_fmt10(K1, K2, K3, K4, K5, K6, K7, K8, K9, K10)\
	"{"K1","K2","K3","K4","K5","K6","K7","K8","K9","K10"}"
	
#define JSON_fmt11(K1, K2, K3, K4, K5, K6, K7, K8, K9, K10, K11)\
	"{"K1","K2","K3","K4","K5","K6","K7","K8","K9","K10","K11"}"
	

#define JOBJ_fmt1(name, K)\
	JSON_key(name)JSON_fmt1(K)

#define JOBJ_fmt2(name, K1, K2)\
	JSON_key(name)JSON_fmt2(K1, K2)

#define JOBJ_fmt3(name, K1, K2, K3)\
	JSON_key(name)JSON_fmt3(K1, K2, K3)

#define JOBJ_fmt4(name, K1, K2, K3,  K4)\
	JSON_key(name)JSON_fmt4(K1, K2, K3, K4)

#define JOBJ_fmt5(name, K1, K2, K3,  K4, K5)\
	JSON_key(name)JSON_fmt5(K1, K2, K3, K4, K5)

#define JOBJ_fmt6(name, K1, K2, K3,  K4, K5, K6)\
	JSON_key(name)JSON_fmt6(K1, K2, K3, K4, K5, K6)

#define JOBJ_fmt7(name, K1, K2, K3,  K4, K5, K6, K7)\
	JSON_key(name)JSON_fmt7(K1, K2, K3, K4, K5, K6, K7)

#define JOBJ_fmt8(name, K1, K2, K3, K4, K5, K6, K7, K8)\
	JSON_key(name)JSON_fmt8(K1, K2, K3, K4, K5, K6, K7, K8)

#define JOBJ_fmt9(name, K1, K2, K3, K4, K5, K6, K7, K8, K9)\
	JSON_key(name)JSON_fmt9(K1, K2, K3, K4, K5, K6, K7, K8, K9)

#define JOBJ_fmt10(name, K1, K2, K3, K4, K5, K6, K7, K8, K9, K10)\
	JSON_key(name)JSON_fmt10(K1, K2, K3, K4, K5, K6, K7, K8, K9, K10)

#define JOBJ_fmt11(name, K1, K2, K3, K4, K5, K6, K7, K8, K9, K10, K11)\
	JSON_key(name)JSON_fmt11(K1, K2, K3, K4, K5, K6, K7, K8, K9, K10, K11)

#endif
