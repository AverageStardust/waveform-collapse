#include "superposition.h"

typedef enum {
	NONE = -1,
	RIGHT,
	TOP,
	LEFT,
	BOTTOM
} TileEdge;

void superposition_free(Superposition* superposition) {
	free_inst(superposition->temp_edge_field);
	free_inst(superposition->temp_tile_field);
	free_inst(superposition->fields);

	entropies_free(superposition->entropies);
	hashmap_free(superposition->stale_entropy_tiles, NULL);

	free_inst(superposition);
}

// update the entory for one tile, the value of this node in the hashmap is the superposition
void* update_stale_entropies_map_func(uint64_t key, void* value) {
	Superposition* superposition = (Superposition*)value;
	int i = x_from_hashkey(key), j = y_from_hashkey(key);

	// get tile field
	int tile_field_size = superposition->world->tileset->tile_field_size;
	int tile_index = i + j * superposition->collapse_width;
	BitField tile_field = field_index_array(superposition->fields, tile_field_size, tile_index);

	// find entropy of tile giving distribution
	distribution_area_select(superposition->area, superposition->u + i, superposition->v + j);
	Entropy new_entropy = distribution_area_get_shannon_entropy(tile_field);

	entropies_update_entropy(superposition->entropies, i + j * superposition->collapse_width, new_entropy);

	// since this is a map function we should return the value of this node
	return superposition;
}

// update entropy data for recetly changed tiles
void update_stale_entropies(Superposition* superposition) {
	hashmap_map(superposition->stale_entropy_tiles, update_stale_entropies_map_func);

	// clear hashmap
	hashmap_clear(superposition->stale_entropy_tiles, 32);
}

// collapse tile with least entropy

void constrain_neighbours(Superposition* superposition, int u, int v, TileEdge skip_edge);

void constrain_field(Superposition* superposition, int i, int j, BitField edge_constraint, int from_edge) {
	if (i < 0 || j < 0 || i >= superposition->collapse_width || j >= superposition->collapse_height) return;
	if (entropies_is_collapsed(superposition->entropies, i + j * superposition->collapse_width)) return;

	Tileset* tileset = superposition->world->tileset;
	BitField tile_field = field_index_array(superposition->fields, tileset->tile_field_size, i + j * superposition->collapse_width);

	int inital_pop = field_popcnt(tile_field, tileset->tile_field_size);
	tileset_constrain_tile(tileset, tile_field, edge_constraint, from_edge);
	int final_pop = field_popcnt(tile_field, tileset->tile_field_size);

	// check if there was a change
	if (inital_pop != final_pop) {
		// record that entorpy is stale, the update is delayed incase it is done repeatedly in a short time
		// it's convinent to give a pointer to superposition for later, see update_stale_entropies
		if (superposition->record_entropy_changes)
			hashmap_set(superposition->stale_entropy_tiles, hashkey_from_pair(i, j), superposition);

		// propogate change to neighbours
		constrain_neighbours(superposition, i, j, from_edge);
	}
}

void constrain_neighbours(Superposition* superposition, int i, int j, TileEdge skip_edge) {
	Tileset* tileset = superposition->world->tileset;
	BitField tile_field = field_index_array(superposition->fields, tileset->tile_field_size, i + j * superposition->collapse_width);

	if (skip_edge != RIGHT) {
		tileset_find_tile_edge(tileset, tile_field, superposition->temp_edge_field, RIGHT);
		constrain_field(superposition, i + 1, j, superposition->temp_edge_field, LEFT);
	}

	if (skip_edge != TOP) {
		tileset_find_tile_edge(tileset, tile_field, superposition->temp_edge_field, TOP);
		constrain_field(superposition, i, j + 1, superposition->temp_edge_field, BOTTOM);
	}

	if (skip_edge != LEFT) {
		tileset_find_tile_edge(tileset, tile_field, superposition->temp_edge_field, LEFT);
		constrain_field(superposition, i - 1, j, superposition->temp_edge_field, RIGHT);
	}

	if (skip_edge != BOTTOM) {
		tileset_find_tile_edge(tileset, tile_field, superposition->temp_edge_field, BOTTOM);
		constrain_field(superposition, i, j - 1, superposition->temp_edge_field, TOP);
	}
}

void collapse_least(Superposition* superposition) {
	Tileset* tileset = superposition->world->tileset;

	// pick tile with least entropy
	GenerationTile least_tile = entropies_collapse_least(superposition->entropies);

	int i = least_tile % superposition->collapse_width, j = least_tile / superposition->collapse_width;

	// get field for tile
	BitField tile_field = field_index_array(superposition->fields, tileset->tile_field_size, i + j * superposition->collapse_width);

	// collapse to tile using weighted random
	distribution_area_select(superposition->area, superposition->u + i, superposition->v + j);
	int tile_id = distribution_area_pick_random(tile_field);

	// update world
	world_set(superposition->world, superposition->x + superposition->u + i, superposition->y + superposition->v + j, tile_id);

	// update field
	field_clear(tile_field, tileset->tile_field_size);
	field_set_bit(tile_field, tile_id);

	// propogate change to neighbours
	constrain_neighbours(superposition, i, j, NONE);

	// clean up entropies for next pick
	update_stale_entropies(superposition);
}

void get_naive_tile_field(Superposition* superposition, int i, int j, BitField tile_field) {
	int tile_id = world_get(superposition->world, superposition->x + superposition->u + i, superposition->y + superposition->v + j);
	Tileset* tileset = superposition->world->tileset;

	if (tile_id == NULL_TILE) {
		distribution_area_select(superposition->area, superposition->u + i, superposition->u + j);
		distribution_area_get_all_tiles(tile_field, tileset->tile_field_size);
	} else {
		field_clear(tile_field, tileset->tile_field_size);
		field_set_bit(tile_field, tile_id);
	}
}

int superposition_collapse_tiles(Superposition* superposition, int amount) {
	for (int i = 0; i < amount; i++) {
		if (superposition->entropies->heap_size <= 0) return 1;
		collapse_least(superposition);
	}

	return 0;
}

void superposition_select_collapse_area(Superposition* superposition, int u, int v, int width, int height) {
	superposition->u = u;
	superposition->v = v;
	superposition->collapse_width = width;
	superposition->collapse_height = height;

	Tileset* tileset = superposition->world->tileset;
	superposition->fields = field_create_empty_array(width * height, tileset->tile_field_size);

	// disable entropy while we construct the inital feilds
	superposition->record_entropy_changes = 0;

	// get naive values for each tile feild
	for (int i = 0; i < width; i++) {
		for (int j = 0; j < height; j++) {
			BitField tile_field = field_index_array(superposition->fields, tileset->tile_field_size, i + j * width);
			get_naive_tile_field(superposition, i, j, tile_field);
		}
	}

	// contrain tiles baced off horizontal edges
	for (int i = 0; i < width; i++) {
		get_naive_tile_field(superposition, i, 0, superposition->temp_tile_field);
		tileset_find_tile_edge(tileset, superposition->temp_tile_field, superposition->temp_edge_field, TOP);
		constrain_field(superposition, i, 0, superposition->temp_edge_field, BOTTOM);

		get_naive_tile_field(superposition, i, height - 1, superposition->temp_tile_field);
		tileset_find_tile_edge(tileset, superposition->temp_tile_field, superposition->temp_edge_field, BOTTOM);
		constrain_field(superposition, i, height - 1, superposition->temp_edge_field, TOP);
	}

	// contrain tiles baced off vertical edges
	for (int j = 0; j < height; j++) {
		get_naive_tile_field(superposition, 0, j, superposition->temp_tile_field);
		tileset_find_tile_edge(tileset, superposition->temp_tile_field, superposition->temp_edge_field, RIGHT);
		constrain_field(superposition, 0, j, superposition->temp_edge_field, LEFT);

		get_naive_tile_field(superposition, width - 1, j, superposition->temp_tile_field);
		tileset_find_tile_edge(tileset, superposition->temp_tile_field, superposition->temp_edge_field, LEFT);
		constrain_field(superposition, width - 1, j, superposition->temp_edge_field, RIGHT);
	}

	// contrain tiles baced off eachother
	for (int j = 0; j < height; j++) {
		for (int i = 0; i < width; i++) {
			constrain_neighbours(superposition, i, j, NONE);
		}
	}

	// calculate entropies for each tile
	for (int i = 0; i < width; i++) {
		for (int j = 0; j < height; j++) {
			int tile_id = world_get(superposition->world, superposition->x + u + i, superposition->y + v + j);

			if (tile_id != NULL_TILE) {
				// already collapsed, skip entropy calculation
				superposition->entropies->tiles[i + j * width] = COLLAPSED_ENTROPY;
				continue;
			}

			BitField tile_field = field_index_array(superposition->fields, tileset->tile_field_size, i + j * width);

			distribution_area_select(superposition->area, u + i, v + j);
			Entropy entropy = distribution_area_get_shannon_entropy(tile_field);

			superposition->entropies->tiles[i + j * width] = entropy;
		}
	}

	entropies_initalize_from_tiles(superposition->entropies, width, height);

	hashmap_clear(superposition->stale_entropy_tiles, 32);
	superposition->record_entropy_changes = 1;
}

void superposition_select_distribution_area(Superposition* superposition, int x, int y, DistributionArea* area) {
	superposition->x = x;
	superposition->y = y;
	superposition->area = area;
}

Superposition* superposition_create(World* world) {
	Superposition* superposition = malloc_inst(sizeof(Superposition));

	if (superposition == NULL) {
		fprintf(stderr, "Failed to allocate memory: superposition_create()\n");
		exit(1);
	}

	superposition->world = world;
	superposition->entropies = entropies_create(world->chunk_size, world->chunk_size);
	superposition->stale_entropy_tiles = hashmap_create(32);

	superposition->temp_edge_field = field_create(world->tileset->edge_field_size);
	superposition->temp_tile_field = field_create(world->tileset->tile_field_size);

	return superposition;
}