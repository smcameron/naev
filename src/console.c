/*
 * See Licensing and Copyright notice in naev.h
 */

/**
 * @file console.c
 *
 * @brief Handles the Lua console.
 */

#include "console.h"

#include "naev.h"

#include <stdlib.h>
#include "nstring.h"

#define lua_c
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "log.h"
#include "naev.h"
#include "nlua.h"
#include "nlua_cli.h"
#include "nlua_tk.h"
#include "nlua_tex.h"
#include "nlua_col.h"
#include "nlua_bkg.h"
#include "nlua_camera.h"
#include "nlua_music.h"
#include "font.h"
#include "toolkit.h"
#include "nfile.h"
#include "menu.h"
#include "conf.h"
#include "array.h"


#define BUTTON_WIDTH    50 /**< Button width. */
#define BUTTON_HEIGHT   20 /**< Button height. */


/*
 * Global stuff.
 */
static nlua_env cli_env     = LUA_NOREF; /**< Lua CLI env. */
static glFont *cli_font     = NULL; /**< CLI font to use. */

/*
 * Buffers.
 */
#define CLI_MAX_INPUT      1024 /** Maximum characters typed into console. */
#define CLI_WIDTH          (SCREEN_W - 100) /**< Console width. */
#define CLI_HEIGHT         (SCREEN_H - 100) /**< Console height. */
static char **cli_buffer; /**< CLI buffer. */
static int cli_history     = 0; /**< Position in history. */
static int cli_firstOpen   = 1; /**< First time opening. */


/*
 * Input handling.
 */
static int cli_firstline   = 1; /**< Is this the first line? */


/*
 * CLI stuff.
 */
static int cli_script( lua_State *L );
static int cli_printOnly( lua_State *L );
static const luaL_Reg cli_methods[] = {
   { "print", cli_printOnly },
   { "script", cli_script },
   { "warn", cli_warn },
   {NULL, NULL}
}; /**< Console only functions. */



/*
 * Prototypes.
 */
static int cli_keyhandler( unsigned int wid, SDLKey key, SDLMod mod );
static void cli_render( double bx, double by, double w, double h, void *data );
static int cli_printCore( lua_State *L, int cli_only );


/**
 * @brief Back end for the Lua print functionality.
 */
static int cli_printCore( lua_State *L, int cli_only )
{
   int n; /* number of arguments */
   int i;
   char buf[CLI_MAX_INPUT];
   int p;
   const char *s;

   n = lua_gettop(L);
   p = 0;

   lua_getglobal(L, "tostring");
   for (i=1; i<=n; i++) {
      lua_pushvalue(L, -1);  /* function to be called */
      lua_pushvalue(L, i);   /* value to print */
      lua_call(L, 1, 1);
      s = lua_tostring(L, -1);  /* get result */
      if (s == NULL)
         return luaL_error(L, LUA_QL("tostring") " must return a string to "
               LUA_QL("print"));
      if (!cli_only)
         LOG( "%s", s );

      /* Add to console. */
      p += nsnprintf( &buf[p], CLI_MAX_INPUT-p, "%s%s", (i>1) ? "   " : "", s );
      if (p >= CLI_MAX_INPUT) {
         cli_addMessage(buf);
         p = 0;
      }
      lua_pop(L, 1);  /* pop result */
   }

   /* Add last line if needed. */
   if (n > 0)
      cli_addMessage(buf);

   return 0;
}


/**
 * @brief Barebones warn implementation for Lua, allowing scripts to print warnings to stderr.
 *
 * @luafunc warn()
 */
int cli_warn( lua_State *L )
{
   const char *msg;

   msg = luaL_checkstring(L,1);
   logprintf( stderr, "Warning: %s\n", msg );

   return 0;
}


/**
 * @brief Replacement for the internal Lua print to print to both the console and the terminal.
 */
int cli_print( lua_State *L )
{
   return cli_printCore( L, 0 );
}


/**
 * @brief Replacement for the internal Lua print to print to console instead of terminal.
 */
static int cli_printOnly( lua_State *L )
{
   return cli_printCore( L, 1 );
}


/**
 * @brief Would be like "dofile" from the base Lua lib.
 */
static int cli_script( lua_State *L )
{
   const char *fname;
   char buf[PATH_MAX], *bbuf;
   int n;

   /* Handle parameters. */
   fname = luaL_optstring(L, 1, NULL);
   n     = lua_gettop(L);

   /* Try to find the file if it exists. */
   if (nfile_fileExists(fname))
      nsnprintf( buf, sizeof(buf), "%s", fname );
   else {
      bbuf = strdup( naev_binary() );
      nsnprintf( buf, sizeof(buf), "%s/%s", nfile_dirname( bbuf ), fname );
      free(bbuf);
   }

   /* Do the file. */
   if (luaL_loadfile(L, buf) != 0)
      lua_error(L);

   /* Return the stuff. */
   lua_call(L, 0, LUA_MULTRET);
   return lua_gettop(L) - n;
}


/**
 * @brief Adds a message to the buffer.
 *
 *    @param msg Message to add.
 */
void cli_addMessage( const char *msg )
{
   /* Not initialized. */
   if (cli_env == LUA_NOREF)
      return;

   array_grow(&cli_buffer) = strdup((msg != NULL) ? msg : "");

   cli_history = array_size(cli_buffer) - 1;
}


/**
 * @brief Render function for the custom widget.
 */
static void cli_render( double bx, double by, double w, double h, void *data )
{
   (void) data;
   int i;

   /* Draw the text. */
   for (i=0; i<array_size(cli_buffer); i++)
      gl_printMaxRaw( cli_font, w, bx,
            by + h - (i+1)*(cli_font->h+5),
            &cBlack, cli_buffer[i] );
}


/**
 * @brief Key handler for the console window.
 */
static int cli_keyhandler( unsigned int wid, SDLKey key, SDLMod mod )
{
   (void) mod;
   int i;

   switch (key) {

      /* Go up in history. */
      case SDLK_UP:
         for (i=cli_history-1; i>=0; i--) {
            if (strncmp(cli_buffer[i], "\eD>", 3) == 0) {
               window_setInput( wid, "inpInput", cli_buffer[i]+5 );
               cli_history = i;
               return 1;
            }
         }
         return 1;

      /* Go down in history. */
      case SDLK_DOWN:
         /* Clears buffer. */
         if (cli_history >= array_size(cli_buffer)-1) {
            window_setInput( wid, "inpInput", NULL );
            return 1;
         }

         /* Find next buffer. */
         for (i=cli_history+1; i<array_size(cli_buffer); i++) {
            if (strncmp(cli_buffer[i], "\eD>", 3) == 0) {
               window_setInput( wid, "inpInput", cli_buffer[i]+5 );
               cli_history = i;
               return 1;
            }
         }
         cli_history = i-1;
         window_setInput( wid, "inpInput", NULL );
         return 1;

      default:
         break;
   }

   return 0;
}


/**
 * @brief Initializes the CLI environment.
 */
int cli_init (void)
{
   /* Already loaded. */
   if (cli_env != LUA_NOREF)
      return 0;

   /* Create the state. */
   cli_env = nlua_newEnv();
   nlua_loadTex( cli_env, 0 );
   nlua_loadCol( cli_env, 0 );
   nlua_loadBackground( cli_env, 0 );
   lua_rawgeti(naevL, LUA_REGISTRYINDEX, cli_env);
   nlua_loadCamera( cli_env, 0 );
   nlua_loadTk( NULL );
   nlua_loadCLI( NULL );
   nlua_loadMusic( NULL, 0 );
   luaL_register( naevL, NULL, cli_methods );
   lua_settop( naevL, 0 );

   /* Set the font. */
   cli_font    = malloc( sizeof(glFont) );
   gl_fontInit( cli_font, "dat/mono.ttf", conf.font_size_console );

   /* Allocate the buffer. */
   cli_buffer = array_create(char*);

   return 0;
}


/**
 * @brief Destroys the CLI environment.
 */
void cli_exit (void)
{
   int i;

   /* Destroy the state. */
   if (cli_env != LUA_NOREF) {
      nlua_freeEnv( cli_env );
      cli_env = LUA_NOREF;
   }

   /* Free the buffer. */
   for (i=0; i<array_size(cli_buffer); i++)
      free(cli_buffer[i]);
   array_free(cli_buffer);
   cli_buffer = NULL;
}


/**
 * @brief Handles the CLI input.
 *
 *    @param wid Window receiving the input.
 *    @param unused Unused.
 */
static void cli_input( unsigned int wid, char *unused )
{
   (void) unused;
   int status;
   char *str;
   char buf[CLI_MAX_INPUT+7];

   /* Get the input. */
   str = window_getInput( wid, "inpInput" );

   /* Ignore useless stuff. */
   if (str == NULL)
      return;

   /* Put the message in the console. */
   nsnprintf( buf, CLI_MAX_INPUT+7, "\eD%s %s\e0",
         cli_firstline ? "> " : ">>", str );
   cli_addMessage( buf );

   /* Set up for concat. */
   if (!cli_firstline)               /* o */
      lua_pushliteral(naevL, "\n");  /* o \n */

   /* Load the string. */
   lua_pushstring( naevL, str );     /* s */

   /* Concat. */
   if (!cli_firstline)               /* o \n s */
      lua_concat(naevL, 3);          /* s */

   status = luaL_loadbuffer( naevL, lua_tostring(naevL,-1), lua_strlen(naevL,-1), "=cli" );

   /* Mark as console. */
   lua_pushboolean( naevL, 1 );
   lua_setglobal( naevL, "__cli" );

   /* String isn't proper Lua yet. */
   if (status == LUA_ERRSYNTAX) {
      size_t lmsg;
      const char *msg = lua_tolstring(naevL, -1, &lmsg);
      const char *tp = msg + lmsg - (sizeof(LUA_QL("<eof>")) - 1);
      if (strstr(msg, LUA_QL("<eof>")) == tp) {
         /* Pop the loaded buffer. */
         lua_pop(naevL, 1);
         cli_firstline = 0;
      }
      else {
         /* Real error, spew message and break. */
         cli_addMessage( lua_tostring(naevL, -1) );
         lua_settop(naevL, 0);
         cli_firstline = 1;
      }
   }

   /* Print results - all went well. */
   else if (status == 0) {
      lua_remove(naevL,1);

      lua_rawgeti(naevL, LUA_REGISTRYINDEX, cli_env);
      lua_setfenv(naevL, -2);

      if (lua_pcall(naevL, 0, LUA_MULTRET, 0)) {
         cli_addMessage( lua_tostring(naevL, -1) );
         lua_pop(naevL, 1);
      }
      if (lua_gettop(naevL) > 0) {
         nlua_getenv(cli_env, "print");
         lua_insert(naevL, 1);
         if (lua_pcall(naevL, lua_gettop(naevL)-1, 0, 0) != 0)
            cli_addMessage( "Error printing results." );
      }

      /* Clear stack. */
      lua_settop(naevL, 0);
      cli_firstline = 1;
   }

   /* Unset __cli. */
   lua_pushnil( naevL );
   lua_setglobal( naevL, "__cli" );

   /* Clear the box now. */
   window_setInput( wid, "inpInput", NULL );
}


/**
 * @brief Opens the console.
 */
void cli_open (void)
{
   unsigned int wid;

   /* Lazy loading. */
   if (cli_env == LUA_NOREF)
      if (cli_init())
         return;

   /* Make sure main menu isn't open. */
   if (menu_isOpen(MENU_MAIN))
      return;

   /* Must not be already open. */
   if (window_exists( "Lua Console" ))
      return;

   /* Put a friendly message at first. */
   if (cli_firstOpen) {
      char buf[256];
      cli_addMessage( "" );
      cli_addMessage( "\egWelcome to the Lua console!" );
      nsnprintf( buf, sizeof(buf), "\eg "APPNAME" v%s", naev_version(0) );
      cli_addMessage( buf );
      cli_addMessage( "" );
      cli_firstOpen = 0;
   }

   /* Create the window. */
   wid = window_create( "Lua Console", -1, -1, CLI_WIDTH, CLI_HEIGHT );

   /* Window settings. */
   window_setAccept( wid, cli_input );
   window_setCancel( wid, window_close );
   window_handleKeys( wid, cli_keyhandler );

   /* Input box. */
   window_addInput( wid, 20, 20,
         CLI_WIDTH-60-BUTTON_WIDTH, BUTTON_HEIGHT,
         "inpInput", CLI_MAX_INPUT, 1, cli_font );

   /* Buttons. */
   window_addButton( wid, -20, 20, BUTTON_WIDTH, BUTTON_HEIGHT,
         "btnClose", "Close", window_close );

   /* Custom console widget. */
   window_addCust( wid, 20, -40,
         CLI_WIDTH-40, CLI_HEIGHT-80-BUTTON_HEIGHT,
         "cstConsole", 0, cli_render, NULL, NULL );
}


