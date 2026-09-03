#include "nebu_scripting.h"

#include "lua.h"
#include "lualib.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// static lua_State *L;
lua_State *L;

extern void init_c_interface(lua_State *L);

void scripting_Init() {
  L = lua_open(0);
  lua_baselibopen(L);
  lua_strlibopen(L);
  lua_iolibopen(L);

  // init_c_interface(L);
}

void scripting_Quit() {
  lua_close(L);
}


void showStack() {
	int i;
	printf("dumping stack with %d elements\n", lua_gettop(L));
	for(i = 0; i < lua_gettop(L); i++) {
		int type = lua_type(L, - (i+1));
		switch(type) {
		case LUA_TNIL: printf("nil\n"); break;
		case LUA_TNUMBER: printf("number\n"); break;
		case LUA_TSTRING: printf("string\n"); break;
		case LUA_TTABLE: printf("table\n"); break;
		case LUA_TFUNCTION: printf("function\n"); break;
		case LUA_TUSERDATA: printf("userdata\n"); break;
		}
	}
}

int scripting_IsNilResult() {
	int result = lua_isnil(L, -1);
	lua_pop(L, 1);
	return result;
}

int getGlobal(const char *s, va_list ap) {
	int top = lua_gettop(L);
	int count = 0;
	while(s) {
		lua_pushstring(L, s);
		lua_gettable(L, -2);
		count++;
		s = va_arg(ap, char *);
	}
	lua_insert(L, top); /* move result to bottom */
	lua_pop(L, count); /* restore stack */
	return 0;
}

int scripting_GetGlobal(const char *global, const char *s, ...) {
	lua_getglobal(L, global);
	if(s) {
		va_list ap;
		va_start(ap, s);
		getGlobal(s, ap);
		va_end(ap);
	}
	return 0;
}	

int scripting_SetFloat(float f, const char *name, const char *global, const char *s, ...) {
	va_list ap;
	
	if(global == NULL) {
		lua_pushnumber(L, f);
		lua_setglobal(L, global);
		return 0;
	}
	
	lua_getglobal(L, global);
	
	if(s) {
		va_start(ap, s);
		getGlobal(s, ap);
		va_end(ap);
	}
	
	lua_pushstring(L, name);
	lua_pushnumber(L, f);
	lua_settable(L, -3);
	lua_pop(L, 1);

	return 0;
}

int scripting_GetFloatResult(float *f) {
  int top = lua_gettop(L);

  if(f != NULL)
    *f = 0.0f;
  if(f != NULL && top > 0 && lua_isnumber(L, -1)) {
    *f = (float)lua_tonumber(L, -1);
		lua_pop(L, 1); /* restore stack */
		return 0;
	} else {
		showStack();
    if(top > 0)
      lua_pop(L, 1);
    return 1;
	}
}  

static int getIntegerResult(int *i, int strict) {
  int top = lua_gettop(L);

  if(i != NULL)
    *i = 0;
  if(i != NULL && top > 0 && lua_isnumber(L, -1)) {
    double value = lua_tonumber(L, -1);
    if(value >= INT_MIN && value <= INT_MAX) {
      int integer = (int)value;
      if(!strict || value == (double)integer) {
        *i = integer;
        lua_pop(L, 1); /* restore stack */
        return 0;
      }
    }
  }

  showStack();
  if(top > 0)
    lua_pop(L, 1);
  return 1;
}

int scripting_GetIntegerResult(int *i) {
  return getIntegerResult(i, 0);
}

int scripting_GetStrictIntegerResult(int *i) {
  return getIntegerResult(i, 1);
}  

void scripting_GetFloatArrayResult(float *f, int n) {
  int i;
	
  for(i = 0; i < n; i++) {
    lua_rawgeti(L, -1, i + 1);
    if(lua_isnumber(L, -1)) {
      *(f + i) = (float)lua_tonumber(L, 2);
    } else {
      fprintf(stderr, "element %d is not number!\n", i);
    }
    lua_pop(L, 1); /* remove number from stack */
  }

	lua_pop(L, 1); /* remove table from stack */
}

int scripting_GetStringResult(char **s) {
  int status;
  int top = lua_gettop(L);

  if(s != NULL)
    *s = NULL;
  if(s != NULL && top > 0 && lua_isstring(L, -1)) {
    size_t size;
    status = 0;
    size = lua_strlen(L, -1) + 1;
    *s = malloc( size );
    if(*s != NULL) {
      memcpy(*s, lua_tostring(L, -1), size - 1);
      (*s)[size - 1] = '\0';
    } else {
      status = 2;
    }
  } else
    status = 1;

  if(top > 0)
    lua_pop(L, 1);
  return status;
}

int scripting_CopyStringResult(char *s, int len) {
  int status;
  int top = lua_gettop(L);

  if(s != NULL && len > 0)
    s[0] = '\0';
  if(s != NULL && len > 0 && top > 0 && lua_isstring(L, -1)) {
    size_t size, copy;
    status = 0;
    size = lua_strlen(L, -1);
    if(size >= (size_t)len) {
      copy = (size_t)len - 1;
      status = 2;
    } else {
      copy = size;
    }
    memcpy(s, lua_tostring(L, -1), copy);
    s[copy] = '\0';
  } else
    status = 1;

  if(top > 0)
    lua_pop(L, 1);
  return status;
}    

int scripting_RunFileChecked(const char *name) {
  if(name == NULL)
    return -1;
  return lua_dofile(L, name);
}

int scripting_RunChecked(const char *command) {
  if(command == NULL)
    return -1;
  /* fprintf(stderr, "[command] %s\n", command); */
  return lua_dostring(L, command);
}

static int scripting_RunFormatV(const char *format, va_list ap) {
  char buf[4096];
  int written;

  if(format == NULL)
    return -1;
  written = vsnprintf(buf, sizeof(buf), format, ap);
  if(written < 0 || (size_t)written >= sizeof(buf)) {
    fprintf(stderr, "[scripting] formatted command exceeds %lu bytes\n",
            (unsigned long)(sizeof(buf) - 1));
    return -1;
  }
  return scripting_RunChecked(buf);
}

int scripting_RunFormatChecked(const char *format, ... ) {
  int status;
  va_list ap;

  va_start(ap, format);
  status = scripting_RunFormatV(format, ap);
  va_end(ap);
  return status;
}

void scripting_RunFile(const char *name) {
  (void)scripting_RunFileChecked(name);
}

void scripting_Run(const char *command) {
  (void)scripting_RunChecked(command);
}

void scripting_RunFormat(const char *format, ... ) {
  va_list ap;

  va_start(ap, format);
  (void)scripting_RunFormatV(format, ap);
  va_end(ap);
}

void scripting_RunGC() {
  lua_setgcthreshold(L, 0);
}

void Scripting_Idle() {
	scripting_RunGC();
}

void scripting_Register(const char *name, int(*func) (lua_State *L)) {
	lua_register(L, name, func);
}
