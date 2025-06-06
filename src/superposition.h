#ifndef SUPERPOSITION_GUARD
#define SUPERPOSITION_GUARD

#include <emscripten.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "bitfield.h"
#include "distribution.h"
#include "entropies.h"
#include "hashmap.h"
#include "meminst.h"
#include "world.h"

#define STALE_TILE_LIMIT 256

typedef struct {
	DistributionArea* area;
	World* world;

	BitField temp_tile_field;
	BitField temp_edge_field;
	BitField fields;
	Entropies* entropies;
	Hashmap* stale_entropy_tiles;
	int record_entropy_changes;

	// location of distribution area in world
	int x;
	int y;

	// location and size of collapse area in the distribution area
	int u;
	int v;
	int collapse_width;
	int collapse_height;

} Superposition;

extern EMSCRIPTEN_KEEPALIVE Superposition* superposition_create(World* world);
extern EMSCRIPTEN_KEEPALIVE void superposition_select_distribution_area(Superposition* superposition, int x, int y, DistributionArea* area);
extern EMSCRIPTEN_KEEPALIVE void superposition_select_collapse_area(Superposition* superposition, int u, int v, int width, int height);
extern EMSCRIPTEN_KEEPALIVE int superposition_collapse_tiles(Superposition* superposition, int amount);
extern EMSCRIPTEN_KEEPALIVE void superposition_free(Superposition* superposition);

#endif