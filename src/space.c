

#include "space.h"

#include <malloc.h>
#include <math.h>

#include "libxml/parser.h"

#include "main.h"
#include "log.h"
#include "opengl.h"
#include "rng.h"
#include "pilot.h"
#include "pack.h"

#define MAX_PATH_NAME   30 /* maximum size of the path */

#define XML_NODE_START  1
#define XML_NODE_TEXT   3

#define XML_PLANET_ID	"Planets"
#define XML_PLANET_TAG	"planet"

#define XML_SYSTEM_ID	"Systems"
#define XML_SYSTEM_TAG	"ssys"

#define PLANET_DATA	"dat/planet.xml"
#define SPACE_DATA	"dat/ssys.xml"

#define PLANET_GFX     "gfx/planet/"


/*
 * Planets types, taken from
 * http://en.wikipedia.org/wiki/Star_Trek_planet_classifications
 */
typedef enum { PLANET_CLASS_NULL=0, /* Null/Not defined */
		PLANET_CLASS_A,	/* Geothermal */
		PLANET_CLASS_B,	/* Geomorteus */
		PLANET_CLASS_C,	/* Geoinactive */
		PLANET_CLASS_D,	/* Asteroid/Moon */
		PLANET_CLASS_E,	/* Geoplastic */
		PLANET_CLASS_F,	/* Geometallic */
		PLANET_CLASS_G,	/* GeoCrystaline */
		PLANET_CLASS_H,	/* Desert */
		PLANET_CLASS_I,	/* Gas Supergiant */
		PLANET_CLASS_J,	/* Gas Giant */
		PLANET_CLASS_K,	/* Adaptable */
		PLANET_CLASS_L,	/* Marginal */
		PLANET_CLASS_M,	/* Terrestrial */
		PLANET_CLASS_N,	/* Reducing */
		PLANET_CLASS_O,	/* Pelagic */
		PLANET_CLASS_P,	/* Glaciated */
		PLANET_CLASS_Q,	/* Variable */
		PLANET_CLASS_R,	/* Rogue */
		PLANET_CLASS_S,	/* Ultragiant */
		PLANET_CLASS_T,	/* Ultragiant */
		PLANET_CLASS_X,	/* Demon */
		PLANET_CLASS_Y,	/* Demon */
		PLANET_CLASS_Z		/* Demon */
} PlanetClass;
typedef struct {
	char* name;

	double x, y; /* position in star system */

	PlanetClass class;
	
	gl_texture* gfx_space; /* graphic in space */

} Planet;


/*
 * star systems
 */
typedef struct {
	char* name;

	double x, y; /* position */
	int stars, asteroids; /* in number */
	double interference; /* in % */

	/* faction; */

	Planet *planets; /* planets */
	int nplanets;

	/* fleets; */
} StarSystem;
static StarSystem *systems = NULL;
static int nsystems = 0;
static StarSystem *cur_system = NULL; /* Current star system */


#define STAR_BUF	100	/* area to leave around screen */
typedef struct {
	double x,y; /* position, lighter to use to doubles then the physics system */
	double brightness;
} Star;
static Star *stars = NULL; /* star array */
static int nstars = 0; /* total stars */


/* 
 * Prototypes
 */
static Planet* planet_get( const char* name );
static StarSystem* system_parse( const xmlNodePtr parent );


/*
 * initializes the system
 */
void space_init ( const char* sysname )
{
	int i;

	for (i=0; i < nsystems; i++)
		if (strcmp(sysname, systems[i].name)==0)
			break;

	if (i==nsystems) ERR("System %s not found in stack", sysname);
	cur_system = systems+i;

	nstars = (cur_system->stars*gl_screen.w*gl_screen.h+STAR_BUF*STAR_BUF)/(800*640);
	stars = malloc(sizeof(Star)*nstars);
	for (i=0; i < nstars; i++) {
		stars[i].brightness = (double)RNG( 50, 200 )/256.;
		stars[i].x = (double)RNG( -STAR_BUF, gl_screen.w + STAR_BUF );
		stars[i].y = (double)RNG( -STAR_BUF, gl_screen.h + STAR_BUF );
	}
}


/*
 * loads the planet of name 'name'
 */
static Planet* planet_get( const char* name )
{
	Planet* temp = NULL;

	char str[MAX_PATH_NAME] = "\0";

	uint32_t bufsize;
	char *buf = pack_readfile( DATA, PLANET_DATA, &bufsize );

	xmlNodePtr node, cur;
	xmlDocPtr doc = xmlParseMemory( buf, bufsize );

	node = doc->xmlChildrenNode;
	if (strcmp((char*)node->name,XML_PLANET_ID)) {
		ERR("Malformed "PLANET_DATA"file: missing root element '"XML_PLANET_ID"'");
		return NULL;
	}

	node = node->xmlChildrenNode; /* first system node */
	if (node == NULL) {
		ERR("Malformed "PLANET_DATA" file: does not contain elements");
		return NULL;
	}


	do {
		if (node->type == XML_NODE_START &&
				strcmp((char*)node->name,XML_PLANET_TAG)==0) {
			if (strcmp((char*)xmlGetProp(node,(xmlChar*)"name"),name)==0) { /* found */
				temp = CALLOC_ONE(Planet);
				temp->name = strdup(name);

				node = node->xmlChildrenNode;

				while ((node = node->next)) {
					if (strcmp((char*)node->name,"GFX")==0) {
						cur = node->children;
						if (strcmp((char*)cur->name,"text")==0) {
							snprintf( str, strlen((char*)cur->content)+sizeof(PLANET_GFX),
									PLANET_GFX"%s", (char*)cur->content);
							temp->gfx_space = gl_newSprite(str, 1, 1);
						}
					}
					else if (strcmp((char*)node->name,"pos")==0) {
						cur = node->children;
						while((cur = cur->next)) {
							if (strcmp((char*)cur->name,"x")==0)
								temp->x = atof((char*)cur->children->content);
							else if (strcmp((char*)cur->name,"y")==0)
								temp->y = atof((char*)cur->children->content);
						}
					}
					else if (strcmp((char*)node->name,"general")==0) {
						cur = node->children;
						while((cur = cur->next)) {
							if (strcmp((char*)cur->name,"class")==0)
								temp->class = atoi((char*)cur->children->content);
						}
					}
				}
				break;
			}
		}
	} while ((node = node->next));
	
	xmlFreeDoc(doc);
	free(buf);
	xmlCleanupParser();

	if (temp) {
#define MELEMENT(o,s)	if (o == 0) WARN("Planet '%s' missing '"s"' element", temp->name)
		MELEMENT(temp->x,"x");
		MELEMENT(temp->x,"y");
		MELEMENT(temp->class,"class");
#undef MELEMENT
	}
	else
		WARN("No Planet found matching name '%s'", name);

	return temp;
}


/*
 * parses node 'parent' which should be the node of a system
 * returns the StarSystem fully loaded
 */
static StarSystem* system_parse( const xmlNodePtr parent )
{
	Planet* planet = NULL;
	StarSystem* temp = CALLOC_ONE(StarSystem);
	xmlNodePtr cur, node;

	temp->name = strdup((char*)xmlGetProp(parent,(xmlChar*)"name"));

	node  = parent->xmlChildrenNode;

	while ((node = node->next)) { /* load all the data */
		if (strcmp((char*)node->name,"pos")==0) {
			cur = node->children;
			while((cur = cur->next)) {
				if (strcmp((char*)cur->name,"x")==0)
					temp->x = atof((char*)cur->children->content);
				else if (strcmp((char*)cur->name,"y")==0)
					temp->y = atof((char*)cur->children->content);
			}
		}
		else if (strcmp((char*)node->name,"general")==0) {
			cur = node->children;
			while ((cur = cur->next)) {
				if (strcmp((char*)cur->name,"stars")==0)
					temp->stars = atoi((char*)cur->children->content);
				else if (strcmp((char*)cur->name,"asteroids")==0)
					temp->asteroids = atoi((char*)cur->children->content);
				else if (strcmp((char*)cur->name,"interference")==0)
					temp->interference = atof((char*)cur->children->content)/100.;
			}
		}
		else if (strcmp((char*)node->name,"planets")==0) {
			cur = node->children;
			while((cur = cur->next)) {
				if (strcmp((char*)cur->name,"planet")==0) {
					planet = planet_get((const char*)cur->children->content);
					temp->planets = realloc(temp->planets, sizeof(Planet)*(++temp->nplanets));
					memcpy(temp->planets+temp->nplanets-1, planet, sizeof(Planet));
					free(planet);
				}
			}
		}
	}

#define MELEMENT(o,s)      if (o == 0) WARN("Star System '%s' missing '"s"' element", temp->name)
	if (temp->name == NULL) WARN("Star System '%s' missing 'name' tag", temp->name);
	MELEMENT(temp->x,"x");
	MELEMENT(temp->y,"y");
	MELEMENT(temp->stars,"stars");
	/*MELEMENT(temp->asteroids,"asteroids"); can be 0
	MELEMENT(temp->interference,"inteference");*/
#undef MELEMENT

	DEBUG("Loaded Star System '%s' with %d Planet%s", temp->name,
			temp->nplanets, (temp->nplanets > 1) ? "s" : "" );

	return temp;
}


/*
 * LOADS THE ENTIRE UNIVERSE INTO RAM - pretty big feat eh?
 */
int space_load (void)
{
	uint32_t bufsize;
	char *buf = pack_readfile( DATA, SPACE_DATA, &bufsize );

	StarSystem *temp;

	xmlNodePtr node;
	xmlDocPtr doc = xmlParseMemory( buf, bufsize );

	node = doc->xmlChildrenNode;
	if (strcmp((char*)node->name,XML_SYSTEM_ID)) {
		ERR("Malformed "SPACE_DATA"file: missing root element '"XML_SYSTEM_ID"'");
		return -1;
	}

	node = node->xmlChildrenNode; /* first system node */
	if (node == NULL) {
		ERR("Malformed "SPACE_DATA" file: does not contain elements");
		return -1;
	}

	do {
		if (node->type == XML_NODE_START &&           
				strcmp((char*)node->name,XML_SYSTEM_TAG)==0) {

			if (systems==NULL) {
				systems = temp = system_parse(node);
				nsystems = 1;
			}    
			else {
				temp = system_parse(node);               
				systems = realloc(systems, sizeof(StarSystem)*(++nsystems));
				memcpy(systems+nsystems-1, temp, sizeof(StarSystem));
				free(temp);
			}
		}                                                                             
	} while ((node = node->next));                                                   

	xmlFreeDoc(doc);
	free(buf);
	xmlCleanupParser();

	return 0;
}


/*
 * renders the system
 */
void space_render( double dt )
{
	int i;

	glMatrixMode(GL_PROJECTION);
	glPushMatrix(); /* projection translation matrix */
		glTranslated( -(double)gl_screen.w/2., -(double)gl_screen.h/2., 0.);

	glBegin(GL_POINTS);
	for (i=0; i < nstars; i++) {
		/* update position */
		stars[i].x -= VX(player->solid->vel)/(15.-10.*stars[i].brightness)*dt;
		stars[i].y -= VY(player->solid->vel)/(15.-10.*stars[i].brightness)*dt;
		if (stars[i].x > gl_screen.w + STAR_BUF) stars[i].x = -STAR_BUF;
		else if (stars[i].x < -STAR_BUF) stars[i].x = gl_screen.w + STAR_BUF;
		if (stars[i].y > gl_screen.h + STAR_BUF) stars[i].y = -STAR_BUF;
		else if (stars[i].y < -STAR_BUF) stars[i].y = gl_screen.h + STAR_BUF;
		/* render */
		glColor4d( 1., 1., 1., stars[i].brightness );
		glVertex2d( stars[i].x, stars[i].y );
	}
	glEnd();

	glPopMatrix(); /* projection translation matrix */
}

/*
 * renders the planets
 */
void planets_render (void)
{
	int i;
	Vector2d v;
	for (i=0; i < cur_system->nplanets; i++) {
		v.x = cur_system->planets[i].x;
		v.y = cur_system->planets[i].y;
		gl_blitSprite( cur_system->planets[i].gfx_space, &v, 0, 0 );
	}
}


/*
 * cleans up the system
 */
void space_exit (void)
{
	int i,j;
	for (i=0; i < nsystems; i++) {
		free(systems[i].name);
		for (j=0; j < systems[i].nplanets; j++) {
			free(systems[i].planets[j].name);
			if (systems[i].planets[j].gfx_space)
				gl_freeTexture(systems[i].planets[j].gfx_space);
		}
		free(systems[i].planets);
	}
	free(systems);

	if (stars) free(stars);
}

