import { Distribution } from "./distribution";

let tileset_create: (edge_field_size: number, tile_field_size: number) => number;
let tileset_add_tile: (tileset: number, tile: number, render_data: number, right_edge: number, top_edge: number, left_edge: number, bottom_edge: number) => void;
let tileset_free: (ptr: number) => null;

const tilesetRegistry = new FinalizationRegistry((ptr: number) => {
    tileset_free(ptr);
});

export function init() {
    tileset_create = cwrap("tileset_create", "number", ["number", "number"]);
    tileset_add_tile = cwrap("tileset_add_tile", null, ["number", "number", "number", "number", "number", "number", "number"]);
    tileset_free = cwrap("tileset_free", null, ["number"]);
}

export class Tileset {
    readonly ptr: number;
    readonly edgeLimit: number;
    readonly tileLimit: number;

    tileCount = 0;

    static create(edgeLimit: number = 128, tileLimit: number = 128): Tileset {
        const tileset = new Tileset(tileset_create((edgeLimit + 7) >> 3, (tileLimit + 7) >> 3));
        tilesetRegistry.register(tileset, tileset.ptr, tileset);
        return tileset;
    }

    constructor(ptr: number) {
        this.ptr = ptr;
        this.edgeLimit = getValue(this.ptr + 0, "i32") * 8;
        this.tileLimit = getValue(this.ptr + 4, "i32") * 8;
    }

    addTile(textureId: number, transformation: number, edge: number): number;
    addTile(textureId: number, transformation: number, horizontalEdge: number, verticalEdeg: number): number;
    addTile(textureId: number, transformation: number, rightEdge: number, topEdge: number, leftEdge: number, bottomEdge: number): number;
    addTile(textureId: number, transformation: number, rightEdge: number, topEdge = rightEdge, leftEdge = rightEdge, bottomEdge = topEdge): number {
        const tileId = this.tileCount;

        if (tileId >= this.tileLimit)
            throw Error("Tileset has hit tile limit");

        if (rightEdge >= this.edgeLimit || topEdge >= this.edgeLimit || leftEdge >= this.edgeLimit || bottomEdge >= this.edgeLimit)
            throw Error("Tileset has hit edge limit");

        const renderData = (transformation << 16) + textureId;
        tileset_add_tile(this.ptr, tileId, renderData, rightEdge, topEdge, leftEdge, bottomEdge);

        this.tileCount++;
        return tileId;
    }

    free() {
        tilesetRegistry.unregister(this);
        tileset_free(this.ptr);
    }
}

export class DistributionTileset extends Tileset {
    distributions: Distribution[] = [];

    static create(edgeLimit: number = 128, tileLimit: number = 128): DistributionTileset {
        const tileset = new DistributionTileset(tileset_create((edgeLimit + 7) >> 3, (tileLimit + 7) >> 3));
        tilesetRegistry.register(tileset, tileset.ptr);
        return tileset;
    }

    addDistribution(distribution: Distribution, rightEdge: number, topEdge: number, leftEdge: number, bottomEdge: number): number {
        const tileId = super.addTile(0, 0, rightEdge, topEdge, leftEdge, bottomEdge);
        this.distributions[tileId] = distribution;

        return tileId;
    }

    getDistribution(tileId: number) {
        return this.distributions[tileId];
    }
}