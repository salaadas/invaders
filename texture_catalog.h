#pragma once

#include "common.h"

#include "catalog.h"

#include "texture.h"
using Texture_Catalog = Catalog<Texture_Map>;

void init_texture_catalog(Texture_Catalog *catalog);
// void deinit_texture_catalog(Texture_Catalog *catalog);
Texture_Map *make_placeholder(Texture_Catalog *catalog, String short_name, String full_path);
void reload_asset(Texture_Catalog *catalog, Texture_Map *texture);

void load_texture_from_file(Texture_Map *map, bool is_srgb);
bool load_texture_from_memory(Texture_Map *map, u8 *memory, i64 size_to_read, bool is_srgb);

bool load_dds_texture_helper(Texture_Map *map);

//
// The bitmap doesn't manage the memory of the data, it is up to the caller
// to handle freeing the thing.
// If you want to free the bitmap:
// call: stbi_image_free(bitmap.data)
//
void load_bitmap_from_path(Bitmap *bitmap, String full_path);
