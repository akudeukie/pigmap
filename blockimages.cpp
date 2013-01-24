// Copyright 2010-2012 Michael J. Nelson
//
// This file is part of pigmap.
//
// pigmap is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// pigmap is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with pigmap.  If not, see <http://www.gnu.org/licenses/>.

#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <unordered_map>

#include "blockimages.h"
#include "utils.h"

using namespace std;


// in this file, confusingly, "tile" refers to the texture/image tile, not to the map tiles
// also, this is a nasty mess in here; apologies to anyone reading this


void writeBlockImagesVersion(int B, const string& imgpath, int32_t version)
{
	string versionfile = imgpath + "/blocks-" + tostring(B) + ".version";
	ofstream outfile(versionfile.c_str());
	outfile << version;
}

// get the version number associated with blocks-B.png; this is stored
//  in blocks-B.version, which is just a single string with the version number
int getBlockImagesVersion(int B, const string& imgpath)
{
	string versionfile = imgpath + "/blocks-" + tostring(B) + ".version";
	ifstream infile(versionfile.c_str());
	// if there's no version file, assume the version is 0, so new version file
	// will be generated according to the descriptor file version
	if (infile.fail())
	{
		infile.close();
		writeBlockImagesVersion(B, imgpath, 0);
		return 0;
	}
	// otherwise, read the version
	int32_t v;
	infile >> v;
	// if the version is clearly insane, ignore it
	if (v < 0 || v > 10000)
		v = 0;
	return v;
}

bool BlockImages::create(int B, const string& imgpath)
{
	rectsize = 4*B;

	// 1.5
	// Block mapping is specified in a separate list, pointing to textures listed in another file
	string blocktexturesfile = "blocktextures.list";
	string blockdescriptorfile = "blockdescriptor.list";
	
	ifstream texturelist(blocktexturesfile.c_str());
	ifstream descriptorlist(blockdescriptorfile.c_str());
	setBlockDescriptors(descriptorlist);
	blockversion = setOffsets();
	
	// first, see if blocks-B.png exists, and what its version is
	int biversion = getBlockImagesVersion(B, imgpath);
	string blocksfile = imgpath + "/blocks-" + tostring(B) + ".png";
	if (img.readPNG(blocksfile))
	{
		// if it's the correct size and version, we're okay
		int w = rectsize*16, h = (blockversion/16 + 1) * rectsize;
		if (img.w == w && img.h == h && biversion == blockversion)
		{
			retouchAlphas(B);
			checkOpacityAndTransparency(B);
			return true;
		}
		// if it's a previous version, we will need to build a new one, in case descriptor order
		// has been changed
		if (biversion < blockversion && img.w == w && img.h == (biversion/16 + 1) * rectsize)
		{
			cerr << blocksfile << " is of and older version (" << biversion << ")" << endl;
			cerr << "...new block file will be built (" << blockversion << ")" << endl;
		}
		// otherwise, the file's been trashed somehow; rebuild it
		else
		{
			cerr << blocksfile << " has incorrect size (expected " << w << "x" << h << ")" << endl;
			cerr << "...will try to create new block file" << blocksfile << endl;
		}
	}
	else
		cerr << blocksfile << " not found (or failed to read as PNG); will try to build from provided textures" << endl;

	// build blocks-B.png from list of textures in texture list file
	if(texturelist.fail())
	{
		texturelist.close();
		cerr << blocktexturesfile << " is missing" << endl;
		return false;
	}
	else if(descriptorlist.fail())
	{
		descriptorlist.close();
		cerr << blockdescriptorfile << " is missing" << endl;
		return false;
	}
	else if (!construct(B, texturelist, descriptorlist, imgpath))
	{
		cerr << "image path is missing at least one of the required files" << endl;
		cerr << "from minecraft.jar or your tile pack." << endl;
		cerr << "endportal.png -- included with pigmap" << endl;
		return false;
	}
	
	// write blocks-B.png and blocks-B.version
	img.writePNG(blocksfile);
	writeBlockImagesVersion(B, imgpath, blockversion);

	retouchAlphas(B);
	checkOpacityAndTransparency(B);
	return true;
}



// take the various textures from chest.png and use them to construct "flat" 14x14 tiles (or whatever
//  the multiplied size is, if the textures are larger), then resize those flat images to 2Bx2B
// ...the resulting image will be a 3x1 array of 2Bx2B images: first the top, then the front, then
//  the side
int generateChestTiles(unordered_map<std::string, RGBAImage>& blockTextures, const RGBAImage& texture, const string& name, int B)
{
	int scale = texture.w / 64;
	
	int chestSize = 14 * scale;
	RGBAImage chesttiles;
	chesttiles.create(chestSize*3, chestSize);
	
	// top texture just gets copied straight over
	blit(texture, ImageRect(14*scale, 0, 14*scale, 14*scale), chesttiles, 0, 0);
	
	// front tile gets the front lid texture plus the front bottom texture, then the latch on
	//  top of that
	blit(texture, ImageRect(14*scale, 14*scale, 14*scale, 4*scale), chesttiles, chestSize, 0);
	blit(texture, ImageRect(14*scale, 33*scale, 14*scale, 10*scale), chesttiles, chestSize, 4*scale);
	blit(texture, ImageRect(scale, scale, 2*scale, 4*scale), chesttiles, chestSize + 6*scale, 2*scale);
	
	// side tile gets the side lid texture plus the side bottom texture
	blit(texture, ImageRect(28*scale, 14*scale, 14*scale, 4*scale), chesttiles, chestSize*2, 0);
	blit(texture, ImageRect(28*scale, 33*scale, 14*scale, 10*scale), chesttiles, chestSize*2, 4*scale);

	int tilesize = 2*B;
	for (int x = 0; x < 3; x++)
	{
		RGBAImage img;
		img.create(tilesize, tilesize);
		resize(chesttiles, ImageRect(x*chestSize, 0, chestSize, chestSize),
		       img, ImageRect(0, 0, tilesize, tilesize));
		blockTextures["/" + name + "_" + tostring(x)] = img;
	}
	return 3;
}

// same thing for largechest.png--construct flat tiles, then resize
// ...resulting image is a 7x1 array of 2Bx2B images:
//  -left half of top
//  -right half of top
//  -left half of front
//  -right half of front
//  -left half of back
//  -right half of back
//  -side
int generateLargeChestTiles(unordered_map<std::string, RGBAImage>& blockTextures, const RGBAImage& texture, const string& name, int B)
{
	int scale = texture.w / 128;
	
	int tilesize = 2*B;
	RGBAImage chesttiles;
	chesttiles.create(7*tilesize, tilesize);
	
	// top texture gets copied straight over--note that the original texture is 30x14, but
	//  we're putting it into two squares
	resize(texture, ImageRect(14*scale, 0, 30*scale, 14*scale), chesttiles, ImageRect(0, 0, tilesize*2, tilesize));
	// front tile gets the front lid texture plus the front bottom texture, then the latch
	//  on top of that
	RGBAImage fronttiles;
	fronttiles.create(30*scale, 14*scale);
	blit(texture, ImageRect(14*scale, 14*scale, 30*scale, 4*scale), fronttiles, 0, 0);
	blit(texture, ImageRect(14*scale, 33*scale, 30*scale, 10*scale), fronttiles, 0, 4*scale);
	blit(texture, ImageRect(scale, scale, 2*scale, 4*scale), fronttiles, 14*scale, 2*scale);
	// do two resizes, to make sure the special end processing picks up the latch
	resize(fronttiles, ImageRect(0, 0, 15*scale, 14*scale), chesttiles, ImageRect(2*tilesize, 0, tilesize, tilesize));
	resize(fronttiles, ImageRect(15*scale, 0, 15*scale, 14*scale), chesttiles, ImageRect(3*tilesize, 0, tilesize, tilesize));
	
	// back tile gets the back lid texture plus the back bottom texture
	RGBAImage backtiles;
	backtiles.create(30*scale, 14*scale);
	blit(texture, ImageRect(58*scale, 14*scale, 30*scale, 4*scale), backtiles, 0, 0);
	blit(texture, ImageRect(58*scale, 33*scale, 30*scale, 10*scale), backtiles, 0, 4*scale);
	resize(backtiles, ImageRect(0, 0, 30*scale, 14*scale), chesttiles, ImageRect(4*tilesize, 0, 2*tilesize, tilesize));
	
	// side tile gets the side lid texture plus the side bottom texture
	RGBAImage sidetile;
	sidetile.create(14*scale, 14*scale);
	blit(texture, ImageRect(44*scale, 14*scale, 14*scale, 4*scale), sidetile, 0, 0);
	blit(texture, ImageRect(44*scale, 33*scale, 14*scale, 10*scale), sidetile, 0, 4*scale);
	resize(sidetile, ImageRect(0, 0, 14*scale, 14*scale), chesttiles, ImageRect(6*tilesize, 0, tilesize, tilesize));
	
	for (int x = 0; x < 7; x++)
	{
		RGBAImage img;
		img.create(tilesize, tilesize);
		blit(chesttiles, ImageRect(x*tilesize, 0, tilesize, tilesize), img, 0, 0);
		blockTextures["/" + name + "_" + tostring(x)] = img;
	}
	
	return 3;
}



// iterate over the pixels of a 2B-sized texture tile; used for both source rectangles and
//  destination parallelograms
struct FaceIterator
{
	bool end;  // true if we're done
	int x, y;  // current pixel
	int pos;

	int size;  // number of columns to draw, as well as number of pixels in each
	int deltaY;  // amount to skew y-coord every 2 columns: -1 or 1 for N/S or W/E facing destinations, 0 for source

	FaceIterator(int xstart, int ystart, int dY, int sz)
	{
		size = sz;
		deltaY = dY;
		end = false;
		x = xstart;
		y = ystart;
		pos = 0;
	}

	void advance()
	{
		pos++;
		if (pos >= size*size)
		{
			end = true;
			return;
		}
		y++;
		if (pos % size == 0)
		{
			x++;
			y -= size;
			if (pos % (2*size) == size)
				y += deltaY;
		}
	}
};

// like FaceIterator with no deltaY (for source rectangles), but with the source rotated and/or flipped
struct RotatedFaceIterator
{
	bool end;
	int x, y;
	int pos;

	int size;
	int rot; // 0 = down, then right; 1 = left, then down; 2 = up, then left; 3 = right, then up
	bool flipX;
	int dx1, dy1, dx2, dy2;

	RotatedFaceIterator(int xstart, int ystart, int r, int sz, bool fX)
	{
		size = sz;
		rot = r;
		flipX = fX;
		end = false;
		pos = 0;
		if (rot == 0)
		{
			x = flipX ? (xstart + size - 1) : xstart;
			y = ystart;
			dx1 = 0;
			dy1 = 1;
			dx2 = flipX ? -1 : 1;
			dy2 = 0;
		}
		else if (rot == 1)
		{
			x = flipX ? xstart : (xstart + size - 1);
			y = ystart;
			dx1 = flipX ? 1 : -1;
			dy1 = 0;
			dx2 = 0;
			dy2 = 1;
		}
		else if (rot == 2)
		{
			x = flipX ? xstart : (xstart + size - 1);
			y = ystart + size - 1;
			dx1 = 0;
			dy1 = -1;
			dx2 = flipX ? 1 : -1;
			dy2 = 0;
		}
		else
		{
			x = flipX ? (xstart + size - 1) : xstart;
			y = ystart + size - 1;
			dx1 = flipX ? -1 : 1;
			dy1 = 0;
			dx2 = 0;
			dy2 = -1;
		}
	}

	void advance()
	{
		pos++;
		if (pos >= size*size)
		{
			end = true;
			return;
		}
		x += dx1;
		y += dy1;
		if (pos % size == 0)
		{
			x += dx2;
			y += dy2;
			x -= dx1 * size;
			y -= dy1 * size;
		}
	}
};

// iterate over the pixels of the top face of a block
struct TopFaceIterator
{
	bool end;  // true if we're done
	int x, y;  // current pixel
	int pos;

	int size;  // number of "columns", and number of pixels in each

	TopFaceIterator(int xstart, int ystart, int sz)
	{
		size = sz;
		end = false;
		x = xstart;
		y = ystart;
		pos = 0;
	}

	void advance()
	{
		if ((pos/size) % 2 == 0)
		{
			int m = pos % size;
			if (m == size - 1)
			{
				x += size - 1;
				y -= size/2;
			}
			else if (m == size - 2)
				y++;
			else if (m % 2 == 0)
			{
				x--;
				y++;
			}
			else
				x--;
		}
		else
		{
			int m = pos % size;
			if (m == 0)
				y++;
			else if (m == size - 1)
			{
				x += size - 1;
				y -= size/2 - 1;
			}
			else if (m % 2 == 0)
			{
				x--;
				y++;
			}
			else
				x--;
		}
		pos++;
		if (pos >= size*size)
			end = true;
	}
};


struct SourceTile
{
	const RGBAImage *image;  // or NULL for no tile
	int xpos, ypos;  // tile offset within the image
	int rot;
	bool flipX;
	
	SourceTile(const RGBAImage *img, int r, bool f) : image(img), xpos(0), ypos(0), rot(r), flipX(f) {}
	SourceTile() : image(NULL), xpos(0), ypos(0), rot(0), flipX(false) {}
	bool valid() const {return image != NULL;}
};

// iterate over a square source tile, with possible rotation and flip
struct SourceIterator
{
	SourceIterator(const SourceTile& tile, int tilesize)
		: image(*(tile.image)), faceit(tile.xpos*tilesize, tile.ypos*tilesize, tile.rot, tilesize, tile.flipX) {}
	
	void advance() {faceit.advance();}
	bool end() {return faceit.end;}
	RGBAPixel pixel() {return image(faceit.x, faceit.y);}

	const RGBAImage& image;
	RotatedFaceIterator faceit;
};

// construct a source iterator for a given texture tile with rotation and/or flip
SourceTile blockTile(const RGBAImage& tile, int rot, bool flipX)
{
	return SourceTile(&tile, rot, flipX);
}
SourceTile blockTile(const RGBAImage& tile)
{
	return blockTile(tile, 0, false);
}


int deinterpolate(int targetj, int srcrange, int destrange)
{
	for (int i = 0; i < destrange; i++)
	{
		int j = interpolate(i, destrange, srcrange);
		if (j >= targetj)
			return i;
	}
	return destrange - 1;
}

// draw a normal block image, using three texture tiles (which may be flipped/rotated/missing), and adding a bit of shadow
//  to the N and W faces
void drawRotatedBlockImage(RGBAImage& dest, const ImageRect& drect, const SourceTile& Wface, const SourceTile& Sface, const SourceTile& Uface, int B) // re-oriented
{
	int tilesize = 2*B;
	// N face starts at [0,B]
	if (Wface.valid())
	{
		FaceIterator dstit(drect.x, drect.y + B, 1, tilesize);
		for (SourceIterator srcit(Wface, tilesize); !srcit.end(); srcit.advance(), dstit.advance())
		{
			dest(dstit.x, dstit.y) = srcit.pixel();
			darken(dest(dstit.x, dstit.y), 0.9, 0.9, 0.9);
		}
	}
	// W face starts at [2B,2B]
	if (Sface.valid())
	{
		FaceIterator dstit(drect.x + 2*B, drect.y + 2*B, -1, tilesize);
		for (SourceIterator srcit(Sface, tilesize); !srcit.end(); srcit.advance(), dstit.advance())
		{
			dest(dstit.x, dstit.y) = srcit.pixel();
			darken(dest(dstit.x, dstit.y), 0.8, 0.8, 0.8);
		}
	}
	// U face starts at [2B-1,0]
	if (Uface.valid())
	{
		TopFaceIterator dstit(drect.x + 2*B-1, drect.y, tilesize);
		for (SourceIterator srcit(Uface, tilesize); !srcit.end(); srcit.advance(), dstit.advance())
		{
			dest(dstit.x, dstit.y) = srcit.pixel();
		}
	}
}

// overload of drawRotatedBlockImage taking three .png tiles
void drawBlockImage(RGBAImage& dest, const ImageRect& drect, RGBAImage& Wface, RGBAImage& Sface, RGBAImage& Uface, int B) // re-oriented
{
	drawRotatedBlockImage(dest, drect, blockTile(Wface), blockTile(Sface), blockTile(Uface), B);
}

// draw a block image where the block isn't full height (half-steps, snow, etc.)
// topcutoff is the number of pixels (out of 2B) to chop off the top of the N and W faces
// bottomcutoff is the number of pixels (out of 2B) to chop off the bottom
// if shift is true, we start copying pixels from the very top of the source tile, even if there's a topcutoff
// U face can also be rotated, and N/W faces can be X-flipped (set 0x1 for N, 0x2 for W)
void drawPartialBlockImage(RGBAImage& dest, const ImageRect& drect, RGBAImage& Wface, RGBAImage& Sface, RGBAImage& Uface, int B, bool W, bool S, bool U, int topcutoff, int bottomcutoff, int rot, int flip, bool shift) // re-oriented
{
	int tilesize = 2*B;
	if (topcutoff + bottomcutoff >= tilesize)
		return;
	int end = tilesize - bottomcutoff;
	// W face starts at [0,B]
	if (W)
	{
		FaceIterator dstit(drect.x, drect.y + B, 1, tilesize);
		for (RotatedFaceIterator srcit(0, 0, 0, tilesize, flip & 0x1); !srcit.end; srcit.advance(), dstit.advance())
		{
			if (dstit.pos % tilesize >= topcutoff && dstit.pos % tilesize < end)
			{
				dest(dstit.x, dstit.y) = Wface(srcit.x, srcit.y - (shift ? topcutoff : 0));
				darken(dest(dstit.x, dstit.y), 0.9, 0.9, 0.9);
			}
		}
	}
	// S face starts at [2B,2B]
	if (S)
	{
		FaceIterator dstit(drect.x + 2*B, drect.y + 2*B, -1, tilesize);
		for (RotatedFaceIterator srcit(0, 0, 0, tilesize, flip & 0x2); !srcit.end; srcit.advance(), dstit.advance())
		{
			if (dstit.pos % tilesize >= topcutoff && dstit.pos % tilesize < end)
			{
				dest(dstit.x, dstit.y) = Sface(srcit.x, srcit.y - (shift ? topcutoff : 0));
				darken(dest(dstit.x, dstit.y), 0.8, 0.8, 0.8);
			}
		}
	}
	// U face starts at [2B-1,topcutoff]
	if (U)
	{
		TopFaceIterator dstit(drect.x + 2*B-1, drect.y + topcutoff, tilesize);
		for (RotatedFaceIterator srcit(0, 0, rot, tilesize, false); !srcit.end; srcit.advance(), dstit.advance())
		{
			dest(dstit.x, dstit.y) = Uface(srcit.x, srcit.y);
		}
	}
}
// override drawPartialBlockImage without arguments for optional face drawing (draw all faces by default)
void drawPartialBlockImage(RGBAImage& dest, const ImageRect& drect, RGBAImage& Wface, RGBAImage& Sface, RGBAImage& Uface, int B, int topcutoff, int bottomcutoff, int rot, int flip, bool shift)
{
	drawPartialBlockImage(dest, drect, Wface, Sface, Uface, B, true, true, true, topcutoff, bottomcutoff, rot, flip, shift);
}

// draw two flat copies of a tile intersecting at the block center (saplings, etc.)
void drawItemBlockImage(RGBAImage& dest, const ImageRect& drect, const SourceTile& tile, bool N, bool S, bool W, bool E, int B) // re-oriented
{
	if (!tile.valid())
		return;
	int tilesize = 2*B;
	int cutoff = tilesize/2;
	// S face starting at [B,1.5B] -- eastern half only
	if (E)
	{
		FaceIterator dstit(drect.x + B, drect.y + B*3/2, -1, tilesize);
		for (SourceIterator srcit(tile, tilesize); !srcit.end(); srcit.advance(), dstit.advance())
		{
			if (dstit.pos / tilesize >= cutoff)
				blend(dest(dstit.x, dstit.y), srcit.pixel());
		}
	}
	// W face starting at [B,0.5B]
	if (N || S)
	{
		FaceIterator dstit(drect.x + B, drect.y + B/2, 1, tilesize);
		for (SourceIterator srcit(tile, tilesize); !srcit.end(); srcit.advance(), dstit.advance())
		{
			if ((S && dstit.pos / tilesize >= cutoff) || (N && dstit.pos / tilesize < cutoff))
				blend(dest(dstit.x, dstit.y), srcit.pixel());
		}
	}
	// S face starting at [B,1.5B] -- western half only
	if (W)
	{
		FaceIterator dstit(drect.x + B, drect.y + B*3/2, -1, tilesize);
		for (SourceIterator srcit(tile, tilesize); !srcit.end(); srcit.advance(), dstit.advance())
		{
			if (dstit.pos / tilesize < cutoff)
				blend(dest(dstit.x, dstit.y), srcit.pixel());
		}
	}
}

void drawItemBlockImage(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tile, int B)
{
	drawItemBlockImage(dest, drect, blockTile(tile), true, true, true, true, B);
}


// draw an item block image possibly missing some edges (iron bars, etc.)
void drawPartialItemBlockImage(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tile, int rot, bool flipX, bool N, bool S, bool W, bool E, int B)
{
	drawItemBlockImage(dest, drect, blockTile(tile, rot, flipX), N, S, W, E, B);
}

// draw four flat copies of a tile intersecting in a square (netherwart, etc.)
void drawMultiItemBlockImage(RGBAImage& dest, const ImageRect& drect, const SourceTile& tile, int B)
{
	if (!tile.valid())
		return;
	int tilesize = 2*B;
	// E/W face starting at [0.5B,1.25B]
	{
		FaceIterator dstit(drect.x + B/2, drect.y + B*5/4, -1, tilesize);
		for (SourceIterator srcit(tile, tilesize); !srcit.end(); srcit.advance(), dstit.advance())
		{
			blend(dest(dstit.x, dstit.y), srcit.pixel());
		}
	}
	// E/W face starting at [1.5B,1.75B]
	{
		FaceIterator dstit(drect.x + 3*B/2, drect.y + B*7/4, -1, tilesize);
		for (SourceIterator srcit(tile, tilesize); !srcit.end(); srcit.advance(), dstit.advance())
		{
			blend(dest(dstit.x, dstit.y), srcit.pixel());
		}
	}
	// N/S face starting at [0.5B,0.75B]
	{
		FaceIterator dstit(drect.x + B/2, drect.y + B*3/4, 1, tilesize);
		for (SourceIterator srcit(tile, tilesize); !srcit.end(); srcit.advance(), dstit.advance())
		{
			blend(dest(dstit.x, dstit.y), srcit.pixel());
		}
	}
	// N/S face starting at [1.5B,0.25B]
	{
		FaceIterator dstit(drect.x + 3*B/2, drect.y + B/4, 1, tilesize);
		for (SourceIterator srcit(tile, tilesize); !srcit.end(); srcit.advance(), dstit.advance())
		{
			blend(dest(dstit.x, dstit.y), srcit.pixel());
		}
	}
}

void drawMultiItemBlockImage(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tile, int B)
{
	drawMultiItemBlockImage(dest, drect, blockTile(tile), B);
}

// draw a tile on a single upright face
// 0 = E, 1 = W, 2 = S, 3 = N
// ...handles transparency
void drawSingleFaceBlockImage(RGBAImage& dest, const ImageRect& drect, const SourceTile& tile, int face, int B) // re-oriented maybe?
{
	if (!tile.valid())
		return;
	int tilesize = 2*B;
	int xoff, yoff, deltaY;
	if (face == 0)
	{
		xoff = 2*B;
		yoff = 0;
		deltaY = 1;
	}
	else if (face == 1)
	{
		xoff = 0;
		yoff = B;
		deltaY = 1;
	}
	else if (face == 2)
	{
		xoff = 2*B;
		yoff = 2*B;
		deltaY = -1;
	}
	else
	{
		xoff = 0;
		yoff = B;
		deltaY = -1;
	}
	FaceIterator dstit(drect.x + xoff, drect.y + yoff, deltaY, tilesize);
	for (SourceIterator srcit(tile, tilesize); !srcit.end(); srcit.advance(), dstit.advance())
	{
		blend(dest(dstit.x, dstit.y), srcit.pixel());
	}
}

void drawSingleFaceBlockImage(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tile, int face, int B)
{
	drawSingleFaceBlockImage(dest, drect, blockTile(tile), face, B);
}

// draw part of a tile on a single upright face
// 0 = S, 1 = N, 2 = W, 3 = E
// ...handles transparency
void drawPartialSingleFaceBlockImage(RGBAImage& dest, const ImageRect& drect, const SourceTile& tile, int face, int B, double croptop, double cropbottom, double cropleft, double cropright)
{
	int tilesize = 2*B;
	int xoff, yoff, deltaY;
	if (face == 0)
	{
		xoff = 2*B;
		yoff = 0;
		deltaY = 1;
	}
	else if (face == 1)
	{
		xoff = 0;
		yoff = B;
		deltaY = 1;
	}
	else if (face == 2)
	{
		xoff = 2*B;
		yoff = 2*B;
		deltaY = -1;
	}
	else
	{
		xoff = 0;
		yoff = B;
		deltaY = -1;
	}
	FaceIterator dstit(drect.x + xoff, drect.y + yoff, deltaY, tilesize);
	for (SourceIterator srcit(tile, tilesize); !srcit.end(); srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize >= croptop && dstit.pos % tilesize < tilesize - cropbottom &&
		    dstit.pos / tilesize >= cropleft && dstit.pos / tilesize < tilesize - cropright)
			blend(dest(dstit.x, dstit.y), srcit.pixel());
	}
}

void drawPartialSingleFaceBlockImage(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tile, int face, int B, int croptop, int cropbottom, int cropleft, int cropright)
{
	drawPartialSingleFaceBlockImage(dest, drect, blockTile(tile), face, B, croptop, cropbottom, cropleft, cropright);
}

// draw a single tile on the floor, possibly with rotation
// 0 = top of tile is on E side; 1 = N, 2 = E, 3 = S
void drawFloorBlockImage(RGBAImage& dest, const ImageRect& drect, const SourceTile& tile, int B)
{
	int tilesize = 2*B;
	TopFaceIterator dstit(drect.x + 2*B-1, drect.y + 2*B, tilesize);
	for (SourceIterator srcit(tile, tilesize); !srcit.end(); srcit.advance(), dstit.advance())
	{
		dest(dstit.x, dstit.y) = srcit.pixel();
	}
}

void drawFloorBlockImage(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tile, int rot, int B)
{
	drawFloorBlockImage(dest, drect, blockTile(tile, rot, false), B);
}

// draw a single tile on the floor, possibly with rotation, angled upwards
// rot: 0 = top of tile is on S side; 1 = W, 2 = N, 3 = E
// up: 0 = S side of tile is highest; 1 = W, 2 = N, 3 = E
void drawAngledFloorBlockImage(RGBAImage& dest, const ImageRect& drect, const SourceTile& tile, int up, int B)
{
	int tilesize = 2*B;
	TopFaceIterator dstit(drect.x + 2*B-1, drect.y + 2*B, tilesize);
	for (SourceIterator srcit(tile, tilesize); !srcit.end(); srcit.advance(), dstit.advance())
	{
		int yoff = 0;
		int row = dstit.pos % tilesize, col = dstit.pos / tilesize;
		if (up == 0)
			yoff = tilesize - 1 - row;
		else if (up == 1)
			yoff = col;
		else if (up == 2)
			yoff = row;
		else if (up == 3)
			yoff = tilesize - 1 - col;
		blend(dest(dstit.x, dstit.y - yoff), srcit.pixel());
		blend(dest(dstit.x, dstit.y - yoff + 1), srcit.pixel());
	}
}

void drawAngledFloorBlockImage(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tile, int rot, int up, int B)
{
	drawAngledFloorBlockImage(dest, drect, blockTile(tile, rot, false), up, B);
}

// draw a single tile on the ceiling, possibly with rotation
// 0 = top of tile is on S side; 1 = W, 2 = N, 3 = E
void drawCeilBlockImage(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tile, int rot, int B)
{
	int tilesize = 2*B;
	TopFaceIterator dstit(drect.x + 2*B-1, drect.y, tilesize);
	for (RotatedFaceIterator srcit(0, 0, rot, tilesize, false); !srcit.end; srcit.advance(), dstit.advance())
	{
		dest(dstit.x, dstit.y) = tile(srcit.x, srcit.y);
	}
}

// draw a block image that's just a single color (plus shadows)
void drawSolidColorBlockImage(RGBAImage& dest, const ImageRect& drect, RGBAPixel p, int B)
{
	int tilesize = 2*B;
	// N face starts at [0,B]
	for (FaceIterator dstit(drect.x, drect.y + B, 1, tilesize); !dstit.end; dstit.advance())
	{
		dest(dstit.x, dstit.y) = p;
		darken(dest(dstit.x, dstit.y), 0.9, 0.9, 0.9);
	}
	// W face starts at [2B,2B]
	for (FaceIterator dstit(drect.x + 2*B, drect.y + 2*B, -1, tilesize); !dstit.end; dstit.advance())
	{
		dest(dstit.x, dstit.y) = p;
		darken(dest(dstit.x, dstit.y), 0.8, 0.8, 0.8);
	}
	// U face starts at [2B-1,0]
	for (TopFaceIterator dstit(drect.x + 2*B-1, drect.y, tilesize); !dstit.end; dstit.advance())
	{
		dest(dstit.x, dstit.y) = p;
	}
}

// draw E-ascending stairs
void drawStairsE(RGBAImage& dest, const ImageRect& drect, RGBAImage& tileWS, RGBAImage& tileU, int B)
{
	int tilesize = 2*B;
	// normal W face starts at [0,B]; draw the bottom half of it
	for (FaceIterator srcit(0, 0, 0, tilesize),
	     dstit(drect.x, drect.y + B, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize >= B)
		{
			dest(dstit.x, dstit.y) = tileWS(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.9, 0.9, 0.9);
		}
	}
	// normal S face starts at [2B,2B]; draw all but the upper-left quarter of it
	for (FaceIterator srcit(0, 0, 0, tilesize),
	     dstit(drect.x + 2*B, drect.y + 2*B, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize >= B || dstit.pos / tilesize >= B)
		{
			dest(dstit.x, dstit.y) = tileWS(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.8, 0.8, 0.8);
		}
	}
	// normal U face starts at [2B-1,0]; draw the top half of it
	TopFaceIterator tdstit(drect.x + 2*B-1, drect.y, tilesize);
	for (FaceIterator srcit(0, 0, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		// if B is odd, we need B pixels from each column, but if it's even, we need to alternate between
		//  B-1 and B+1
		int cutoff = B;
		if (B % 2 == 0)
			cutoff += ((tdstit.pos / tilesize) % 2 == 0) ? -1 : 1;
		if (tdstit.pos % tilesize < cutoff)
		{
			dest(tdstit.x, tdstit.y) = tileU(srcit.x, srcit.y);
		}
	}
	// draw the top half of another W face at [B,B/2]
	for (FaceIterator srcit(0, 0, 0, tilesize),
	     dstit(drect.x + B, drect.y + B/2, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		// ...but if B is odd, we need to add an extra [0,1] to the even-numbered columns
		int adjust = 0;
		if (B % 2 == 1 && (dstit.pos / tilesize) % 2 == 0)
			adjust = 1;
		if (dstit.pos % tilesize < B)
		{
			dest(dstit.x, dstit.y + adjust) = tileWS(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y + adjust), 0.9, 0.9, 0.9);
		}
	}
	// draw the bottom half of another U face at [2B-1,B]
	tdstit = TopFaceIterator(drect.x + 2*B-1, drect.y + B, tilesize);
	for (FaceIterator srcit(0, 0, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		// again, if B is odd, take B pixels from each column; if even, take B-1 or B+1
		int cutoff = B;
		if (B % 2 == 0)
			cutoff += ((tdstit.pos / tilesize) % 2 == 0) ? -1 : 1;
		if (tdstit.pos % tilesize >= cutoff)
		{
			dest(tdstit.x, tdstit.y) = tileU(srcit.x, srcit.y);
		}
	}
}

// draw E-ascending stairs inverted
void drawInvStairsE(RGBAImage& dest, const ImageRect& drect, RGBAImage& tileWS, RGBAImage& tileU, int B)
{
	int tilesize = 2*B;
	// draw the bottom half of a W face at [B,B/2]; do this first because the others will partially cover it
	for (FaceIterator srcit(0, 0, 0, tilesize),
	     dstit(drect.x + B, drect.y + B/2, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		// ...but if B is odd, we need to add an extra [0,1] to the even-numbered columns
		int adjust = 0;
		if (B % 2 == 1 && (dstit.pos / tilesize) % 2 == 0)
			adjust = 1;
		if (dstit.pos % tilesize >= B)
		{
			dest(dstit.x, dstit.y + adjust) = tileWS(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y + adjust), 0.9, 0.9, 0.9);
		}
	}
	// normal W face starts at [0,B]; draw the top half of it
	for (FaceIterator srcit(0, 0, 0, tilesize),
	     dstit(drect.x, drect.y + B, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize < B)
		{
			dest(dstit.x, dstit.y) = tileWS(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.9, 0.9, 0.9);
		}
	}
	// normal S face starts at [2B,2B]; draw all but the lower-left quarter of it
	for (FaceIterator srcit(0, 0, 0, tilesize),
	     dstit(drect.x + 2*B, drect.y + 2*B, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize < B || dstit.pos / tilesize >= B)
		{
			dest(dstit.x, dstit.y) = tileWS(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.8, 0.8, 0.8);
		}
	}
	// normal U face starts at [2B-1,0]; draw the whole thing
	TopFaceIterator tdstit(drect.x + 2*B-1, drect.y, tilesize);
	for (FaceIterator srcit(0, 0, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		dest(tdstit.x, tdstit.y) = tileU(srcit.x, srcit.y);
	}
}

// draw W-ascending stairs
void drawStairsW(RGBAImage& dest, const ImageRect& drect, RGBAImage& tileWS, RGBAImage& tileU, int B)
{
	int tilesize = 2*B;
	// draw the top half of an an U face at [2B-1,B]
	TopFaceIterator tdstit(drect.x + 2*B-1, drect.y + B, tilesize);
	for (FaceIterator srcit(0, 0, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		// if B is odd, we need B pixels from each column, but if it's even, we need to alternate between
		//  B-1 and B+1
		int cutoff = B;
		if (B % 2 == 0)
			cutoff += ((tdstit.pos / tilesize) % 2 == 0) ? -1 : 1;
		if (tdstit.pos % tilesize < cutoff)
		{
			dest(tdstit.x, tdstit.y) = tileU(srcit.x, srcit.y);
		}
	}
	// draw the bottom half of the normal U face at [2B-1,0]
	tdstit = TopFaceIterator(drect.x + 2*B-1, drect.y, tilesize);
	for (FaceIterator srcit(0, 0, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		// again, if B is odd, take B pixels from each column; if even, take B-1 or B+1
		int cutoff = B;
		if (B % 2 == 0)
			cutoff += ((tdstit.pos / tilesize) % 2 == 0) ? -1 : 1;
		if (tdstit.pos % tilesize >= cutoff)
		{
			dest(tdstit.x, tdstit.y) = tileU(srcit.x, srcit.y);
		}
	}
	// normal W face starts at [0,B]; draw it all
	for (FaceIterator srcit(0, 0, 0, tilesize),
	     dstit(drect.x, drect.y + B, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		dest(dstit.x, dstit.y) = tileWS(srcit.x, srcit.y);
		darken(dest(dstit.x, dstit.y), 0.9, 0.9, 0.9);
	}
	// normal S face starts at [2B,2B]; draw all but the upper-right quarter of it
	for (FaceIterator srcit(0, 0, 0, tilesize),
	     dstit(drect.x + 2*B, drect.y + 2*B, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize >= B || dstit.pos / tilesize < B)
		{
			dest(dstit.x, dstit.y) = tileWS(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.8, 0.8, 0.8);
		}
	}
}

// draw W-ascending stairs inverted
void drawInvStairsW(RGBAImage& dest, const ImageRect& drect, RGBAImage& tileWS, RGBAImage& tileU, int B)
{
	int tilesize = 2*B;
	// normal U face starts at [2B-1,0]; draw the whole thing
	TopFaceIterator tdstit(drect.x + 2*B-1, drect.y, tilesize);
	for (FaceIterator srcit(0, 0, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		dest(tdstit.x, tdstit.y) = tileU(srcit.x, srcit.y);
	}
	// normal W face starts at [0,B]; draw it all
	for (FaceIterator srcit(0, 0, 0, tilesize),
	     dstit(drect.x, drect.y + B, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		dest(dstit.x, dstit.y) = tileWS(srcit.x, srcit.y);
		darken(dest(dstit.x, dstit.y), 0.9, 0.9, 0.9);
	}
	// normal S face starts at [2B,2B]; draw all but the lower-right quarter of it
	for (FaceIterator srcit(0, 0, 0, tilesize),
	     dstit(drect.x + 2*B, drect.y + 2*B, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize < B || dstit.pos / tilesize < B)
		{
			dest(dstit.x, dstit.y) = tileWS(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.8, 0.8, 0.8);
		}
	}
}

// draw N-ascending stairs
void drawStairsN(RGBAImage& dest, const ImageRect& drect, RGBAImage& tileWS, RGBAImage& tileU, int B)
{
	int tilesize = 2*B;
	// normal W face starts at [0,B]; draw all but the upper-right quarter of it
	for (FaceIterator srcit(0, 0, 0, tilesize),
	     dstit(drect.x, drect.y + B, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize >= B || dstit.pos / tilesize < B)
		{
			dest(dstit.x, dstit.y) = tileWS(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.9, 0.9, 0.9);
		}
	}
	// normal S face starts at [2B,2B]; draw the bottom half of it
	for (FaceIterator srcit(0, 0, 0, tilesize),
	     dstit(drect.x + 2*B, drect.y + 2*B, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize >= B)
		{
			dest(dstit.x, dstit.y) = tileWS(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.8, 0.8, 0.8);
		}
	}
	// normal U face starts at [2B-1,0]; draw the left half of it
	TopFaceIterator tdstit(drect.x + 2*B-1, drect.y, tilesize);
	int tcutoff = tilesize * B;
	bool textra = false;
	// if B is odd, we need to skip the last pixel of the last left-half column, and add the very first
	//  pixel of the first right-half column
	if (B % 2 == 1)
	{
		tcutoff--;
		textra = true;
	}
	for (FaceIterator srcit(0, 0, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		if (tdstit.pos < tcutoff || (textra && tdstit.pos == tcutoff + 1))
		{
			dest(tdstit.x, tdstit.y) = tileU(srcit.x, srcit.y);
		}
	}
	// draw the top half of another S face at [B,1.5B]
	for (FaceIterator srcit(0, 0, 0, tilesize),
	     dstit(drect.x + B, drect.y + 3*B/2, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		// ...but if B is odd, we need to add an extra [0,1] to the odd-numbered columns
		int adjust = 0;
		if (B % 2 == 1 && (dstit.pos / tilesize) % 2 == 1)
			adjust = 1;
		if (dstit.pos % tilesize < B)
		{
			dest(dstit.x, dstit.y + adjust) = tileWS(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y + adjust), 0.8, 0.8, 0.8);
		}
	}
	// draw the right half of another U face at [2B-1,B]
	tdstit = TopFaceIterator(drect.x + 2*B-1, drect.y + B, tilesize);
	tcutoff = tilesize * B;
	textra = false;
	// if B is odd, do the reverse of what we did with the top half
	if (B % 2 == 1)
	{
		tcutoff++;
		textra = true;
	}
	for (FaceIterator srcit(0, 0, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		if (tdstit.pos >= tcutoff || (textra && tdstit.pos == tcutoff - 2))
		{
			dest(tdstit.x, tdstit.y) = tileU(srcit.x, srcit.y);
		}
	}
}

// draw N-ascending stairs inverted
void drawInvStairsN(RGBAImage& dest, const ImageRect& drect, RGBAImage& tileWS, RGBAImage& tileU, int B)
{
	int tilesize = 2*B;
	// draw the bottom half of a S face at [B,1.5B]; do this first because the others will partially cover it
	for (FaceIterator srcit(0, 0, 0, tilesize),
	     dstit(drect.x + B, drect.y + 3*B/2, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		// ...but if B is odd, we need to add an extra [0,1] to the odd-numbered columns
		int adjust = 0;
		if (B % 2 == 1 && (dstit.pos / tilesize) % 2 == 1)
			adjust = 1;
		if (dstit.pos % tilesize >= B)
		{
			dest(dstit.x, dstit.y + adjust) = tileWS(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y + adjust), 0.8, 0.8, 0.8);
		}
	}
	// normal S face starts at [2B,2B]; draw the top half of it
	for (FaceIterator srcit(0, 0, 0, tilesize),
	     dstit(drect.x + 2*B, drect.y + 2*B, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize < B)
		{
			dest(dstit.x, dstit.y) = tileWS(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.8, 0.8, 0.8);
		}
	}
	// normal W face starts at [0,B]; draw all but the lower-right quarter of it
	for (FaceIterator srcit(0, 0, 0, tilesize),
	     dstit(drect.x, drect.y + B, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize < B || dstit.pos / tilesize < B)
		{
			dest(dstit.x, dstit.y) = tileWS(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.9, 0.9, 0.9);
		}
	}
	// normal U face starts at [2B-1,0]; draw the whole thing
	TopFaceIterator tdstit(drect.x + 2*B-1, drect.y, tilesize);
	for (FaceIterator srcit(0, 0, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		dest(tdstit.x, tdstit.y) = tileU(srcit.x, srcit.y);
	}
}

// draw S-ascending stairs
void drawStairsS(RGBAImage& dest, const ImageRect& drect, RGBAImage& tileWS, RGBAImage& tileU, int B)
{
	int tilesize = 2*B;
	// draw the left half of an U face at [2B-1,B]
	TopFaceIterator tdstit(drect.x + 2*B-1, drect.y + B, tilesize);
	int tcutoff = tilesize * B;
	bool textra = false;
	// if B is odd, we need to skip the last pixel of the last left-half column, and add the very first
	//  pixel of the first right-half column
	if (B % 2 == 1)
	{
		tcutoff--;
		textra = true;
	}
	for (FaceIterator srcit(0, 0, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		if (tdstit.pos < tcutoff || (textra && tdstit.pos == tcutoff + 1))
		{
			dest(tdstit.x, tdstit.y) = tileU(srcit.x, srcit.y);
		}
	}
	// draw the right half of the normal U face at [2B-1,0]
	tdstit = TopFaceIterator(drect.x + 2*B-1, drect.y, tilesize);
	tcutoff = tilesize * B;
	textra = false;
	// if B is odd, do the reverse of what we did with the top half
	if (B % 2 == 1)
	{
		tcutoff++;
		textra = true;
	}
	for (FaceIterator srcit(0, 0, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		if (tdstit.pos >= tcutoff || (textra && tdstit.pos == tcutoff - 2))
		{
			dest(tdstit.x, tdstit.y) = tileU(srcit.x, srcit.y);
		}
	}
	// normal W face starts at [0,B]; draw all but the upper-left quarter of it
	for (FaceIterator srcit(0, 0, 0, tilesize),
	     dstit(drect.x, drect.y + B, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize >= B || dstit.pos / tilesize >= B)
		{
			dest(dstit.x, dstit.y) = tileWS(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.9, 0.9, 0.9);
		}
	}
	// normal S face starts at [2B,2B]; draw the whole thing
	for (FaceIterator srcit(0, 0, 0, tilesize),
	     dstit(drect.x + 2*B, drect.y + 2*B, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		dest(dstit.x, dstit.y) = tileWS(srcit.x, srcit.y);
		darken(dest(dstit.x, dstit.y), 0.8, 0.8, 0.8);
	}
}

// draw S-ascending stairs inverted
void drawInvStairsS(RGBAImage& dest, const ImageRect& drect, RGBAImage& tileWS, RGBAImage& tileU, int B)
{
	int tilesize = 2*B;
	// normal U face starts at [2B-1,0]; draw the whole thing
	TopFaceIterator tdstit(drect.x + 2*B-1, drect.y, tilesize);
	for (FaceIterator srcit(0, 0, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		dest(tdstit.x, tdstit.y) = tileU(srcit.x, srcit.y);
	}
	// normal S face starts at [2B,2B]; draw the whole thing
	for (FaceIterator srcit(0, 0, 0, tilesize),
	     dstit(drect.x + 2*B, drect.y + 2*B, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		dest(dstit.x, dstit.y) = tileWS(srcit.x, srcit.y);
		darken(dest(dstit.x, dstit.y), 0.8, 0.8, 0.8);
	}
	// normal W face starts at [0,B]; draw all but the lower-left quarter of it
	for (FaceIterator srcit(0, 0, 0, tilesize),
	     dstit(drect.x, drect.y + B, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize < B || dstit.pos / tilesize >= B)
		{
			dest(dstit.x, dstit.y) = tileWS(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.9, 0.9, 0.9);
		}
	}
}

// generic functions for drawing separate faces with offsets and cutoffs
// - tint: controls how much face is darkened
// - offset: controls how far face is offset from original cube position inwards
// - croptop: controls how much of the face is cropped from the top
// - cropbottom: controls how much of the face is cropped from the bottom
// - cropleft: controls how much of the face is cropped from the left
// - cropright: controls how much of the face is cropped from the right
void drawOffsetPaddedWFace(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tile, int B, double tint, int offset, int croptop, int cropbottom, int cropleft, int cropright) 
{
	int tilesize = 2 * B;
	// draw N face
	for (FaceIterator srcit(0, 0, 0, tilesize),
		 dstit(drect.x + offset, drect.y + B - deinterpolate(offset, 2*tilesize, tilesize), 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize >= croptop && dstit.pos % tilesize < tilesize - cropbottom && dstit.pos / tilesize >= cropleft && dstit.pos / tilesize < tilesize - cropright)
		{
			dest(dstit.x, dstit.y) = tile(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), tint, tint, tint);
		}
	}
}
void drawOffsetPaddedSFace(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tile, int B, double tint, int offset, int croptop, int cropbottom, int cropleft, int cropright) 
{
	int tilesize = 2 * B;
	// draw W face
	for (FaceIterator srcit(0, 0, 0, tilesize),
	     dstit(drect.x + 2*B - offset, drect.y + 2*B - deinterpolate(offset, 2*tilesize, tilesize), -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if(dstit.pos % tilesize >= croptop && dstit.pos % tilesize < tilesize - cropbottom && dstit.pos / tilesize >= cropleft && dstit.pos / tilesize < tilesize - cropright) {
			dest(dstit.x, dstit.y) = tile(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), tint, tint, tint);
		}
	}
}
void drawOffsetPaddedUFace(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tile, int B, int offset, int croptop, int cropbottom, int cropleft, int cropright) 
{
	int tilesize = 2 * B;
	// draw U face
	TopFaceIterator tdstit(drect.x + 2*B-1, drect.y + offset, tilesize);
	for (FaceIterator srcit(0, 0, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		int adjust = 0;
		if (croptop % 2 == 0)
			adjust += ((tdstit.pos / tilesize) % 2 == 0) ? -1 : 1; // adjust for missing pixels
		if(tdstit.pos % tilesize >= croptop + adjust && tdstit.pos % tilesize < tilesize - cropbottom + adjust && tdstit.pos / tilesize >= cropleft && tdstit.pos / tilesize < tilesize - cropright)
			blend(dest(tdstit.x, tdstit.y),tile(srcit.x, srcit.y));
	}
}
// draw simple fence post (that actually works for different zoom levels)
void drawFencePost(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tile, int B)
{	
	// draw a 2x2 top at [2B-1,B-1]
	for (int y = 0; y < 2; y++)
		for (int x = 0; x < 2; x++)
			dest(drect.x + 2*B - 1 + x, drect.y + B - 1 + y) = tile(x, y);

	// draw a 1x2B side at [2B-1,B+1]
	for (int y = 0; y < 2*B; y++)
		dest(drect.x + 2*B - 1, drect.y + B + 1 + y) = tile(0, y);

	// draw a 1x2B side at [2B,B+1]
	for (int y = 0; y < 2*B; y++)
		dest(drect.x + 2*B, drect.y + B + 1 + y) = tile(0, y);
}

// draw simple fence: post and four rails, each optional
void drawFence(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tile, bool N, bool S, bool W, bool E, bool post, int B) // re-oriented
{
	// first, E and S rails, since the post should be in front of them
	int tilesize = 2*B;
	if (N)
	{
		// N/S face starting at [B,0.5B]; left half, one strip
		for (FaceIterator srcit(0, 0, 0, tilesize),
			dstit(drect.x + B, drect.y + B/2, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
		{
			if (dstit.pos / tilesize < B && (((dstit.pos % tilesize) * 2 / B) % 4) == 1)
				dest(dstit.x, dstit.y) = tile(srcit.x, srcit.y);
		}
	}
	if (E)
	{
		// E/W face starting at [B,1.5B]; right half, one strip
		for (FaceIterator srcit(0, 0, 0, tilesize),
			dstit(drect.x + B, drect.y + B*3/2, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
		{
			if (dstit.pos / tilesize >= B && (((dstit.pos % tilesize) * 2 / B) % 4) == 1)
				dest(dstit.x, dstit.y) = tile(srcit.x, srcit.y);
		}
	}

	// now the post
	if (post)
		drawFencePost(dest, drect, tile, B);

	// now the N and W rails
	if (S)
	{
		// N/S face starting at [B,0.5B]; right half, one strip
		for (FaceIterator srcit(0, 0, 0, tilesize),
			dstit(drect.x + B, drect.y + B/2, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
		{
			if (dstit.pos / tilesize >= B && (((dstit.pos % tilesize) * 2 / B) % 4) == 1)
				dest(dstit.x, dstit.y) = tile(srcit.x, srcit.y);
		}
	}
	if (W)
	{
		// E/W face starting at [B,1.5B]; left half, one strip
		for (FaceIterator srcit(0, 0, 0, tilesize),
			dstit(drect.x + B, drect.y + B*3/2, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
		{
			if (dstit.pos / tilesize < B && (((dstit.pos % tilesize) * 2 / B) % 4) == 1)
				dest(dstit.x, dstit.y) = tile(srcit.x, srcit.y);
		}
	}
}
// draw cobblestone/moss wall post
void drawStoneWallPost(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tile, int B)
{	
	int tilesize = 2*B;
	
	int CUTOFF_4_16 = deinterpolate(4, 16, tilesize); // quarter
	
	drawOffsetPaddedWFace(dest, drect, tile, B, 0.8, CUTOFF_4_16, 0, 0, CUTOFF_4_16, CUTOFF_4_16); // offset W face
	drawOffsetPaddedSFace(dest, drect, tile, B, 0.6, CUTOFF_4_16, 0, 0, CUTOFF_4_16, CUTOFF_4_16); // offset S face
	drawOffsetPaddedUFace(dest, drect, tile, B, 0, CUTOFF_4_16, CUTOFF_4_16, CUTOFF_4_16, CUTOFF_4_16); // offset U face
}

// draw solid moss/cobblestone wall
void drawStoneWall(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tile, bool N, int B)
{
	int tilesize = 2*B;
	
	int CUTOFF_3_16 = deinterpolate(3, 16, tilesize); // wall cutoff
	int CUTOFF_5_16 = deinterpolate(5, 16, tilesize); // wall offset
	
	if(N) // cobblestone wall going NS
	{	
		drawOffsetPaddedWFace(dest, drect, tile, B, 0.8, CUTOFF_5_16, CUTOFF_3_16, 0, 0, 0); // offset W face
		drawOffsetPaddedSFace(dest, drect, tile, B, 0.6, 0, CUTOFF_3_16, 0, CUTOFF_5_16, CUTOFF_5_16); // offset S face
		drawOffsetPaddedUFace(dest, drect, tile, B, CUTOFF_3_16, CUTOFF_5_16, CUTOFF_5_16, 0, 0); // offset U face
	}
	else // cobblestone wall going EW
	{
		drawOffsetPaddedWFace(dest, drect, tile, B, 0.8, 0, CUTOFF_3_16, 0, CUTOFF_5_16, CUTOFF_5_16); // offset W face
		drawOffsetPaddedSFace(dest, drect, tile, B, 0.6, CUTOFF_5_16, CUTOFF_3_16, 0, 0, 0); // offset S face
		drawOffsetPaddedUFace(dest, drect, tile, B, CUTOFF_3_16, 0, -1, CUTOFF_5_16, CUTOFF_5_16); // offset U face
	}
}
// draw cobblestone/moss stone post(optional) and any combination of wall rails (N/S/E/W)
void drawStoneWallConnected(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tile, bool N, bool S, bool E, bool W, int B)
{
	int tilesize = 2*B;
	
	int CUTOFF_3_16 = deinterpolate(3, 16, tilesize); // wall cutoff
	int CUTOFF_4_16 = deinterpolate(4, 16, tilesize); // quarter
	int CUTOFF_5_16 = deinterpolate(5, 16, tilesize); // wall offset
	
	// first, N and E rails, since the post should be in front of them
	if (N) // draw N rail of the wall
	{
		drawOffsetPaddedWFace(dest, drect, tile, B, 0.8, CUTOFF_5_16, CUTOFF_3_16, 0, 0, tilesize - CUTOFF_4_16); // offset W face
		drawOffsetPaddedUFace(dest, drect, tile, B, CUTOFF_3_16, CUTOFF_5_16, CUTOFF_5_16, 0, tilesize - CUTOFF_4_16); // offset U face
	}
	if (E) // draw E rail of the wall
	{
		drawOffsetPaddedSFace(dest, drect, tile, B, 0.6, CUTOFF_5_16, CUTOFF_3_16, 0, tilesize - CUTOFF_4_16, 0); // offset S face
		drawOffsetPaddedUFace(dest, drect, tile, B, CUTOFF_3_16, 0, tilesize - CUTOFF_4_16, CUTOFF_5_16, CUTOFF_5_16); // offset U face
	}

	// now the post
	drawStoneWallPost(dest, drect, tile, B);

	// now the S and W rails
	if (S) // draw S rail of the wall
	{
		drawOffsetPaddedWFace(dest, drect, tile, B, 0.8, CUTOFF_5_16, CUTOFF_3_16, 0, tilesize - CUTOFF_4_16, 0); // offset W face
		drawOffsetPaddedSFace(dest, drect, tile, B, 0.6, 0, CUTOFF_3_16, 0, CUTOFF_5_16, CUTOFF_5_16); // offset S face
		drawOffsetPaddedUFace(dest, drect, tile, B, CUTOFF_3_16, CUTOFF_5_16, CUTOFF_5_16, tilesize - CUTOFF_4_16, 0); // offset U face
	}
	if (W) // draw W rail of the wall
	{		
		drawOffsetPaddedWFace(dest, drect, tile, B, 0.8, 0, CUTOFF_3_16, 0, CUTOFF_5_16, CUTOFF_5_16); // offset W face
		drawOffsetPaddedSFace(dest, drect, tile, B, 0.6, CUTOFF_5_16, CUTOFF_3_16, 0, 0, tilesize - CUTOFF_4_16); // offset S face
		drawOffsetPaddedUFace(dest, drect, tile, B, CUTOFF_3_16, tilesize - CUTOFF_4_16, -1, CUTOFF_5_16, CUTOFF_5_16); // offset U face
	}
}

// draw heart of the beacon
void drawBeacon(RGBAImage& dest, const ImageRect& drect, const RGBAImage& pedestalTile, const RGBAImage& heartTile,  const ImageRect& glassrect, int B)
{
	int tilesize = 2*B;
	
	int CUTOFF_2_16 = deinterpolate(2, 16, tilesize); // eighth
	int CUTOFF_3_16 = deinterpolate(3, 16, tilesize); // pedestal height, heart offset
	
	// draw obsidion pedestal
	drawOffsetPaddedWFace(dest, drect, pedestalTile, B, 0.9, CUTOFF_2_16, tilesize - CUTOFF_3_16, 0, CUTOFF_2_16, CUTOFF_2_16); // offset N face
	drawOffsetPaddedSFace(dest, drect, pedestalTile, B, 0.8, CUTOFF_2_16, tilesize - CUTOFF_3_16, 0, CUTOFF_2_16, CUTOFF_2_16); // offset W face
	drawOffsetPaddedUFace(dest, drect, pedestalTile, B, 2*B - CUTOFF_3_16, CUTOFF_2_16, CUTOFF_2_16, CUTOFF_2_16, CUTOFF_2_16); // offset U face
	
	// draw nether star heart
	drawOffsetPaddedWFace(dest, drect, heartTile, B, 0.9, CUTOFF_3_16, CUTOFF_3_16, CUTOFF_3_16, CUTOFF_3_16, CUTOFF_3_16); // offset N face
	drawOffsetPaddedSFace(dest, drect, heartTile, B, 0.8, CUTOFF_3_16, CUTOFF_3_16, CUTOFF_3_16, CUTOFF_3_16, CUTOFF_3_16); // offset W face
	drawOffsetPaddedUFace(dest, drect, heartTile, B, CUTOFF_3_16, CUTOFF_3_16, CUTOFF_3_16, CUTOFF_3_16, CUTOFF_3_16); // offset U face
	
	// blit glass over the drawn beacon heart
	alphablit(dest, glassrect, dest, drect.x, drect.y);
}

// draw oriented anvil; orientation = 0 for NS, 1 - EW orientation
// function does ignore the fact, that anvil facing N is different from one facing S; though difference is so insignificant, that it is deliberately omitted
void drawAnvil(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tile, const RGBAImage& faceTile, int orientation, int B)
{
	int tilesize = 2*B;
	
	int CUTOFF_2_16 = deinterpolate(2, 16, tilesize); // base crop
	int CUTOFF_3_16 = deinterpolate(3, 16, tilesize); // second base crop, face crop
	int CUTOFF_4_16 = deinterpolate(4, 16, tilesize); // quarter
	int CUTOFF_5_16 = deinterpolate(5, 16, tilesize); // second base crop on another side, pillar crop
	int CUTOFF_6_16 = deinterpolate(6, 16, tilesize); // pillar height crops
	int CUTOFF_10_16 = deinterpolate(10, 16, tilesize); // face crop (from bottom)
	
	// draw anvil base (orientation independent)
	drawOffsetPaddedUFace(dest, drect, tile, B, 2*B - CUTOFF_4_16, CUTOFF_2_16, CUTOFF_2_16, CUTOFF_2_16, CUTOFF_2_16); // offset U face
	drawOffsetPaddedWFace(dest, drect, tile, B, 0.85, CUTOFF_2_16, tilesize - CUTOFF_4_16, 0, CUTOFF_2_16, CUTOFF_2_16); // offset W face
	drawOffsetPaddedSFace(dest, drect, tile, B, 0.7, CUTOFF_2_16, tilesize - CUTOFF_4_16, 0, CUTOFF_2_16, CUTOFF_2_16); // offset S face
	
	// draw anvil second base (oriented)
	int noffset = 0;
	int woffset = 0;
	if(orientation == 0)
	{
		noffset = CUTOFF_3_16;
		woffset = CUTOFF_5_16;
	}
	else
	{
		noffset = CUTOFF_5_16;
		woffset = CUTOFF_3_16;
	}
	drawOffsetPaddedUFace(dest, drect, tile, B, 2*B - CUTOFF_5_16, woffset, woffset, noffset, noffset); // offset U face
	drawOffsetPaddedWFace(dest, drect, tile, B, 0.85, woffset, tilesize - CUTOFF_5_16, CUTOFF_4_16, noffset, noffset); // offset W face
	drawOffsetPaddedSFace(dest, drect, tile, B, 0.7, noffset, tilesize - CUTOFF_5_16, CUTOFF_4_16, woffset, woffset); // offset S face
	
	// draw anvil pillar (oriented)
	if(orientation == 0)
	{
		noffset = 0;
		woffset = CUTOFF_2_16;
	}
	else
	{
		noffset = CUTOFF_2_16;
		woffset = 0;
	}
	drawOffsetPaddedWFace(dest, drect, tile, B, 0.85, CUTOFF_4_16 + woffset, CUTOFF_6_16, CUTOFF_5_16, CUTOFF_4_16 + noffset, CUTOFF_4_16 + noffset); // offset W face
	drawOffsetPaddedSFace(dest, drect, tile, B, 0.7, CUTOFF_4_16 + noffset, CUTOFF_6_16, CUTOFF_5_16, CUTOFF_4_16 + woffset, CUTOFF_4_16 + woffset); // offset S face
	
	// draw anvil face (oriented)
	if(orientation == 0)
	{
		noffset = 0;
		woffset = CUTOFF_3_16;
	}
	else
	{
		noffset = CUTOFF_3_16;
		woffset = 0;
	}
	int rot = 0;
	if(orientation == 0)
		rot = 1;
	else
		rot = 0;
	// draw U bottom layer (cropped-texture-fix)
	drawOffsetPaddedUFace(dest, drect, tile, B, 0, woffset, woffset, noffset, noffset); // cropped U face
	
	// draw U face
	TopFaceIterator tdstit = TopFaceIterator(drect.x + 2*B-1, drect.y, tilesize);
	for (RotatedFaceIterator srcit(0, 0, rot, tilesize, 0); !srcit.end; srcit.advance(), tdstit.advance())
	{	
		if(ALPHA(faceTile(srcit.x, srcit.y)) != 0)
			dest(tdstit.x, tdstit.y) = faceTile(srcit.x, srcit.y);
	}
	drawOffsetPaddedWFace(dest, drect, tile, B, 0.85, woffset, 0, CUTOFF_10_16, noffset, noffset); // offset W face
	drawOffsetPaddedSFace(dest, drect, tile, B, 0.7, noffset, 0, CUTOFF_10_16, woffset, woffset); // offset S face
}

// draw hopper
void drawHopper(RGBAImage& dest, const ImageRect& drect, const RGBAImage& baseTile, const RGBAImage& insideTile, int B)
{
	int tilesize = 2*B;
	
	int CUTOFF_4_16 = deinterpolate(4, 16, tilesize); // middle offset, bottom height
	int CUTOFF_6_16 = deinterpolate(6, 16, tilesize); // top and middle height
	int CUTOFF_10_16 = deinterpolate(10, 16, tilesize); // top height from bottom fix
	
	// draw funnel
	drawOffsetPaddedWFace(dest, drect, baseTile, B, 0.85, CUTOFF_6_16, 2*CUTOFF_6_16, 0, CUTOFF_6_16, CUTOFF_6_16); // offset W face
	drawOffsetPaddedSFace(dest, drect, baseTile, B, 0.7, CUTOFF_6_16, 2*CUTOFF_6_16, 0, CUTOFF_6_16, CUTOFF_6_16); // offset S face
	
	// draw middle part
	drawOffsetPaddedWFace(dest, drect, baseTile, B, 0.85, CUTOFF_4_16, CUTOFF_6_16, CUTOFF_4_16, CUTOFF_4_16, CUTOFF_4_16); // offset W face
	drawOffsetPaddedSFace(dest, drect, baseTile, B, 0.7, CUTOFF_4_16, CUTOFF_6_16, CUTOFF_4_16, CUTOFF_4_16, CUTOFF_4_16); // offset S face
	
	// draw hopper top back part
	drawPartialSingleFaceBlockImage(dest, drect, baseTile, 3, B, 0, CUTOFF_10_16, 0, 0);  // facing S (N wall)
	drawPartialSingleFaceBlockImage(dest, drect, baseTile, 0, B, 0, CUTOFF_10_16, 0, 0);  // facing W (E wall)
	// draw hopper inside
	drawOffsetPaddedUFace(dest, drect, insideTile, B, CUTOFF_4_16, 0, 0, 0, 0); // inside offset U face
	// draw hopper top front part
	drawOffsetPaddedWFace(dest, drect, baseTile, B, 0.85, 0, 0, CUTOFF_10_16, 0, 0); // offset W face
	drawOffsetPaddedSFace(dest, drect, baseTile, B, 0.7, 0, 0, CUTOFF_10_16, 0, 0); // offset S face
	
} 

// draw empty flower pot
void drawFlowerPot(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tile, const RGBAImage& fillerTile, bool drawContent, const RGBAImage& contentTile, int contentType, int B)
{
	int tilesize = 2*B;
	
	int CUTOFF_4_16 = deinterpolate(4, 16, tilesize); // offset, side crop
	int CUTOFF_5_16 = deinterpolate(5, 16, tilesize); // offset, side crop
	int CUTOFF_6_16 = deinterpolate(6, 16, tilesize); // dirt crop
	int CUTOFF_10_16 = deinterpolate(10, 16, tilesize); // top crop
	int CUTOFF_11_16 = deinterpolate(11, 16, tilesize); // inner side offset
	int CUTOFF_12_16 = deinterpolate(12, 16, tilesize); // dirt offset
	
	// draw background pot faces
	drawOffsetPaddedWFace(dest, drect, tile, B, 0.9, CUTOFF_11_16, CUTOFF_10_16, 0, CUTOFF_5_16, CUTOFF_5_16); // offset W face
	drawOffsetPaddedSFace(dest, drect, tile, B, 0.8, CUTOFF_11_16, CUTOFF_10_16, 0, CUTOFF_5_16, CUTOFF_5_16); // offset S face
	drawOffsetPaddedUFace(dest, drect, fillerTile, B, CUTOFF_12_16, CUTOFF_6_16, CUTOFF_6_16, CUTOFF_6_16, CUTOFF_6_16); // cropped U face
	// draw cactus, if possible
	if(drawContent && contentType == 1) {
		drawOffsetPaddedWFace(dest, drect, contentTile, B, 0.9, CUTOFF_6_16, 0, CUTOFF_4_16, CUTOFF_6_16, CUTOFF_6_16); // offset W face
		drawOffsetPaddedSFace(dest, drect, contentTile, B, 0.8, CUTOFF_6_16, 0, CUTOFF_4_16, CUTOFF_6_16, CUTOFF_6_16); // offset S face
		drawOffsetPaddedUFace(dest, drect, contentTile, B, 0, CUTOFF_6_16, CUTOFF_6_16, CUTOFF_6_16, CUTOFF_6_16); // cropped U face
	}
	// draw front pot faces
	drawOffsetPaddedWFace(dest, drect, tile, B, 0.9, CUTOFF_5_16, CUTOFF_10_16, 0, CUTOFF_5_16, CUTOFF_5_16); // offset W face
	drawOffsetPaddedSFace(dest, drect, tile, B, 0.8, CUTOFF_5_16, CUTOFF_10_16, 0, CUTOFF_5_16, CUTOFF_5_16); // offset S face
	
	// draw multipart/sprite contents of the pot
	if(drawContent && contentType == 0)
		drawItemBlockImage(dest, ImageRect(drect.x, drect.y - CUTOFF_4_16, drect.w, drect.h), contentTile, B);
}

// draw crappy sign facing out towards the viewer
void drawSign(RGBAImage& dest, const ImageRect& drect, const RGBAImage& faceTile, const RGBAImage& poleTile, int B)
{
	// start with fence post
	drawFencePost(dest, drect, poleTile, B);  // fence post
	
	int tilesize = 2*B;
	// draw the top half of a tile at [B,B]
	for (FaceIterator srcit(0, 0, 0, tilesize),
	     dstit(drect.x + B, drect.y + B, 0, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize < B)
			dest(dstit.x, dstit.y) = faceTile(srcit.x, srcit.y);
	}
}

// draw crappy wall lever
void drawWallLever(RGBAImage& dest, const ImageRect& drect, const RGBAImage& baseTile, const RGBAImage& leverTile, int face, int B)
{
	int CUTOFF_8_16 = deinterpolate(8, 16, 2*B);
	int CUTOFF_5_16 = deinterpolate(5, 16, 2*B);
	drawPartialSingleFaceBlockImage(dest, drect, baseTile, face, B, CUTOFF_8_16, 0, CUTOFF_5_16, CUTOFF_5_16);
	drawSingleFaceBlockImage(dest, drect, leverTile, face, B);
}

void drawFloorLeverNS(RGBAImage& dest, const ImageRect& drect, const RGBAImage& baseTile, const RGBAImage& leverTile, int B)
{
	int CUTOFF_5_16 = deinterpolate(5, 16, 2*B);
	int CUTOFF_4_16 = deinterpolate(4, 16, 2*B);
	int CUTOFF_16_16 = deinterpolate(16, 16, 2*B);
	drawOffsetPaddedUFace(dest, drect, baseTile, B, CUTOFF_16_16, CUTOFF_5_16, CUTOFF_5_16, CUTOFF_4_16, CUTOFF_4_16);
	drawItemBlockImage(dest, drect, leverTile, B);
}

void drawFloorLeverEW(RGBAImage& dest, const ImageRect& drect, const RGBAImage& baseTile, const RGBAImage& leverTile, int B)
{
	int CUTOFF_5_16 = deinterpolate(5, 16, 2*B);
	int CUTOFF_4_16 = deinterpolate(4, 16, 2*B);
	int CUTOFF_16_16 = deinterpolate(16, 16, 2*B);
	drawOffsetPaddedUFace(dest, drect, baseTile, B, CUTOFF_16_16, CUTOFF_4_16, CUTOFF_4_16, CUTOFF_5_16, CUTOFF_5_16);
	drawItemBlockImage(dest, drect, leverTile, B);
}

void drawCeilLever(RGBAImage& dest, const ImageRect& drect, const RGBAImage& leverTile, int B)
{
	drawItemBlockImage(dest, drect, blockTile(leverTile, 2, false), true, true, true, true, B);
}

void drawRepeater(RGBAImage& dest, const ImageRect& drect, const RGBAImage& baseTile, const RGBAImage& torchTile, int rot, int B)
{
	drawFloorBlockImage(dest, drect, baseTile, rot, B);
	drawItemBlockImage(dest, drect, torchTile, B);
}

// draw crappy brewing stand: full base tile plus item-shaped stand
void drawBrewingStand(RGBAImage& dest, const ImageRect& drect, RGBAImage& base, RGBAImage& stand, int B)
{
	drawFloorBlockImage(dest, drect, blockTile(base), B);
	drawItemBlockImage(dest, drect, stand, B);
}

void drawCauldron(RGBAImage& dest, const ImageRect& drect, RGBAImage& side, RGBAImage& liquid, int cutoff, int B)
{
	SourceTile sideTile = blockTile(side);
	// start with E/S sides, since liquid goes in front of them
	drawSingleFaceBlockImage(dest, drect, sideTile, 0, B);
	drawSingleFaceBlockImage(dest, drect, sideTile, 3, B);
	
	// draw the liquid
	if (cutoff > 0)
		drawPartialBlockImage(dest, drect, liquid, liquid, liquid, B, false, false, true, cutoff, 0, 0, 0, true);
	
	// now the N/W sides
	drawSingleFaceBlockImage(dest, drect, sideTile, 1, B);
	drawSingleFaceBlockImage(dest, drect, sideTile, 2, B);
}

void drawAnchoredFace(RGBAImage& dest, const ImageRect& drect, const RGBAImage& tile, int B, bool N, bool S, bool W, bool E, bool U)
{
	if (N)
		drawSingleFaceBlockImage(dest, drect, tile, 3, B);
	if (S)
		drawSingleFaceBlockImage(dest, drect, tile, 2, B);
	if (W)
		drawSingleFaceBlockImage(dest, drect, tile, 1, B);
	if (E)
		drawSingleFaceBlockImage(dest, drect, tile, 0, B);
	if (U)
		drawCeilBlockImage(dest, drect, tile, 0, B);
}

// draw crappy dragon egg--just a half-size block
void drawDragonEgg(RGBAImage& dest, const ImageRect& drect, RGBAImage& tile, int B)
{
	int tilesize = 2*B;
	// N face at [0,0.5B]; draw the bottom-right quarter of it
	for (FaceIterator srcit(0, 0, 0, tilesize),
	     dstit(drect.x, drect.y + B/2, 1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize >= B && dstit.pos / tilesize >= B)
		{
			dest(dstit.x, dstit.y) = tile(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.9, 0.9, 0.9);
		}
	}
	// W face at [2B,1.5B]; draw the bottom-left quarter of it
	for (FaceIterator srcit(0, 0, 0, tilesize),
	     dstit(drect.x + 2*B, drect.y + 3*B/2, -1, tilesize); !srcit.end; srcit.advance(), dstit.advance())
	{
		if (dstit.pos % tilesize >= B && dstit.pos / tilesize < B)
		{
			dest(dstit.x, dstit.y) = tile(srcit.x, srcit.y);
			darken(dest(dstit.x, dstit.y), 0.8, 0.8, 0.8);
		}
	}
	// draw the bottom-right quarter of a U face at [2B-1,0.5B]
	TopFaceIterator tdstit(drect.x + 2*B-1, drect.y + B/2, tilesize);
	for (FaceIterator srcit(0, 0, 0, tilesize); !srcit.end; srcit.advance(), tdstit.advance())
	{
		// again, if B is odd, take B pixels from each column; if even, take B-1 or B+1
		int cutoff = B;
		if (B % 2 == 0)
			cutoff += ((tdstit.pos / tilesize) % 2 == 0) ? -1 : 1;
		if (tdstit.pos % tilesize >= cutoff && tdstit.pos / tilesize >= cutoff)
		{
			dest(tdstit.x, tdstit.y) = tile(srcit.x, srcit.y);
		}
	}
}




int offsetIdx(uint16_t blockID, uint8_t blockData)
{
	return blockID * 16 + blockData;
}

void setOffsetsForID(uint16_t blockID, int offset, BlockImages& bi)
{
	int start = blockID * 16;
	int end = start + 16;
	fill(bi.blockOffsets + start, bi.blockOffsets + end, offset);
}

void BlockImages::setBlockDescriptors(ifstream& descriptorlist)
{
	// parse block descriptor list
	int desversion = 0;
	descriptorlist.clear();
	descriptorlist.seekg(0, ios_base::beg);
	string descriptorline;
	
	while(getline(descriptorlist, descriptorline))
	{
		vector<string> blockDescriptor;
		if(descriptorline.size() > 0 && descriptorline[0] != '#') 
		{
			istringstream descriptorlinestream(descriptorline);
			while (!descriptorlinestream.eof())
			{
				string descriptorfield;
				getline( descriptorlinestream, descriptorfield, ' ' );
				if(descriptorfield[0] == '#')
					break;
				blockDescriptor.push_back(descriptorfield);
				desversion++;
			}
			blockDescriptors.push_back(blockDescriptor);
		}
	}
}

int BlockImages::setOffsets()
{
	// default is the dummy image
	fill(blockOffsets, blockOffsets + 4096*16, 0);
	
	// Fill in offset depending on block type
	int offsetIterator = 1;
	int64_t blockid;
	for(vector< vector<string> >::iterator it = blockDescriptors.begin(); it != blockDescriptors.end(); ++it)
	{
		vector<string> descriptor = (*it);
		if(fromstring(descriptor[0], blockid))
		{
			int descriptorsize = descriptor.size();
			if(descriptor[1] == "SOLID")
			{
				if(descriptorsize == 3 || descriptorsize == 5)
					setOffsetsForID(blockid, offsetIterator, *this);
			}
			else if(descriptor[1] == "SOLIDORIENTED")
			{
				if(descriptorsize == 9) 
				{
					// orientation bits order: N S W E - descriptor[2 3 4 5]
					int orientationoffset;
					setOffsetsForID(blockid, offsetIterator, *this); // default orientation assumed N, so skip checking N offset descriptor
					if(!fromstring(descriptor[5], orientationoffset))
						orientationoffset = 5;
					blockOffsets[offsetIdx(blockid, orientationoffset)] = offsetIterator; // oriented E, since front is facing from viewer, it is same as N facing block
					if(!fromstring(descriptor[3], orientationoffset))
						orientationoffset = 3;
					blockOffsets[offsetIdx(blockid, orientationoffset)] = ++offsetIterator; // oriented S
					if(!fromstring(descriptor[4], orientationoffset))
						orientationoffset = 4;
					blockOffsets[offsetIdx(blockid, orientationoffset)] = ++offsetIterator; // oriented W
				}
			}
			else if(descriptor[1] == "SOLIDROTATED")
			{
				if(descriptorsize == 5) 
				{
					// piston value order: down / top / N / S / W / E
					// piston extension bit: 0x8 (top bit)
					setOffsetsForID(blockid, offsetIterator, *this);
					blockOffsets[offsetIdx(blockid, 0)] = blockOffsets[offsetIdx(blockid, 8)] = offsetIterator; // facing Down
					blockOffsets[offsetIdx(blockid, 1)] = blockOffsets[offsetIdx(blockid, 9)] = ++offsetIterator; // facing Top
					blockOffsets[offsetIdx(blockid, 2)] = blockOffsets[offsetIdx(blockid, 10)] = ++offsetIterator; // facing N
					blockOffsets[offsetIdx(blockid, 3)] = blockOffsets[offsetIdx(blockid, 11)] = ++offsetIterator; // facing S
					blockOffsets[offsetIdx(blockid, 4)] = blockOffsets[offsetIdx(blockid, 12)] = ++offsetIterator; // facing W
					blockOffsets[offsetIdx(blockid, 5)] = blockOffsets[offsetIdx(blockid, 13)] = ++offsetIterator; // facing E
				}
			}
			else if(descriptor[1] == "SOLIDDATA")
			{
				if(descriptorsize > 2)
				{
					setOffsetsForID(blockid, offsetIterator, *this);
					for(int i = 0; i < descriptorsize - 2; i++, offsetIterator++)
					{
						blockOffsets[offsetIdx(blockid, i)] = offsetIterator;
					}
					continue;
				}
			}
			else if(descriptor[1] == "SOLIDDATAFILL")
			{
				if(descriptorsize > 2)
				{
					int datasize = descriptorsize - 2;
					for(int i = 0; i < descriptorsize - 2; i++, offsetIterator++)
					{
						for(int di = i; di < 16; di += datasize)
							blockOffsets[offsetIdx(blockid, di)] = offsetIterator;
					}
					continue;
				}
			}
			else if(descriptor[1] == "SOLIDDATATRUNK")
			{
				if(descriptorsize % 2 == 0 && descriptorsize > 3)
				{
					setOffsetsForID(blockid, offsetIterator, *this);
					for(int i = 0; i < descriptorsize - 2; i += 2, offsetIterator++)
						blockOffsets[offsetIdx(blockid, i/2)] = offsetIterator;
					continue;
				}
			}
			else if(descriptor[1] == "SOLIDDATATRUNKROTATED")
			{
				if((descriptorsize - 3) % 3 == 0 && descriptorsize > 3)
				{
					int orientationFlag;
					int groupSize = (descriptorsize - 3)/3;
					bool dataOrdered = (descriptor[2] == "O") ? true : false;
					setOffsetsForID(blockid, offsetIterator, *this);
					for(int i = 0, j = 0; i < descriptorsize - 3 && j < 16; i += 3, j++, offsetIterator++)
					{
						blockOffsets[offsetIdx(blockid, j)] = offsetIterator;
						
						if(!fromstring(descriptor[i + 3], orientationFlag))
							orientationFlag = 0;
						if(dataOrdered && orientationFlag == 1)
						{
							blockOffsets[offsetIdx(blockid, ++j)] = ++offsetIterator; // trunk EW
							blockOffsets[offsetIdx(blockid, ++j)] = ++offsetIterator; // trunk NS
						}
						else if(orientationFlag == 1)
						{
							blockOffsets[offsetIdx(blockid, j + groupSize)] = ++offsetIterator; // trunk EW
							blockOffsets[offsetIdx(blockid, j + 2*groupSize)] = ++offsetIterator; // trunk NS
						}
					}
					continue;
				}
			}
			else if(descriptor[1] == "SOLIDOBSTRUCTED")
			{
				if(descriptorsize == 3 || descriptorsize == 5)
				{
					setOffsetsForID(blockid, offsetIterator, *this);
					offsetIterator += 3;// reserve space for special cases of missing faces facing viewer (W, S, W+S)
				}
			}
			else if(descriptor[1] == "SOLIDPARTIAL")
			{
				if(descriptorsize == 7)
				{
					setOffsetsForID(blockid, offsetIterator, *this);
				}
			}
			else if(descriptor[1] == "SOLIDDATAPARTIALFILL")
			{
				int datasize = descriptorsize - 5;
				if(datasize % 2 == 0)
				{
					for(int i = 0; i < datasize/2; i++, offsetIterator++)
						for(int di = i; di < 16; di += datasize)
						{
							blockOffsets[offsetIdx(blockid, di)] = offsetIterator;
						}
					continue;
				}
			}
			else if(descriptor[1] == "SOLIDTRANSPARENT")
			{
				if(descriptorsize == 8)
				{
					setOffsetsForID(blockid, offsetIterator, *this);
				}
			}
			else if(descriptor[1] == "SLABDATA")
			{
				if(descriptorsize > 2)
				{
					for(int i = 0; i < descriptorsize - 2; i++, offsetIterator++)
					{
						// bottom slab
						blockOffsets[offsetIdx(blockid, i)] = offsetIterator;
						// inverted slab
						blockOffsets[offsetIdx(blockid, i + 8)] = ++offsetIterator;
					}
					continue;
				}
			}
			else if(descriptor[1] == "SLABDATATRUNK")
			{
				if(descriptorsize % 2 == 0 && descriptorsize > 3)
				{
					setOffsetsForID(blockid, offsetIterator, *this);
					for(int i = 0; i < descriptorsize - 2; i += 2, offsetIterator++)
					{
						// bottom slab
						blockOffsets[offsetIdx(blockid, i/2)] = offsetIterator;
						// inverted slab
						blockOffsets[offsetIdx(blockid, i/2 + 8)] = ++offsetIterator;
					}
					continue;
				}
			}
			else if(descriptor[1] == "ITEMDATA" || descriptor[1] == "MULTIITEMDATA")
			{
				if(descriptorsize > 2)
				{
					setOffsetsForID(blockid, offsetIterator, *this);
					for(int i = 0; i < descriptorsize - 2; i++, offsetIterator++)
					{
						blockOffsets[offsetIdx(blockid, i)] = offsetIterator;
					}
					continue;
				}
			}
			else if(descriptor[1] == "ITEMDATAORIENTED")
			{
				if(descriptorsize > 6)
				{
					int datasize = descriptorsize - 6;
					// orientation bits order: N S W E - descriptor[2 3 4 5]
					int orientationoffset;
					setOffsetsForID(blockid, offsetIterator, *this);
					for(int i = 0; i < datasize && i < 4; i++, offsetIterator++)
					{
						if(!fromstring(descriptor[2], orientationoffset))
							orientationoffset = 0;
						blockOffsets[offsetIdx(blockid, orientationoffset + 4*i)] = offsetIterator; // N
						if(!fromstring(descriptor[3], orientationoffset))
							orientationoffset = 2;
						blockOffsets[offsetIdx(blockid, orientationoffset + 4*i)] = ++offsetIterator; // S
						if(!fromstring(descriptor[4], orientationoffset))
							orientationoffset = 3;
						blockOffsets[offsetIdx(blockid, orientationoffset + 4*i)] = ++offsetIterator; // W
						if(!fromstring(descriptor[5], orientationoffset))
							orientationoffset = 1;
						blockOffsets[offsetIdx(blockid, orientationoffset + 4*i)] = ++offsetIterator; // E
					}
					continue;
				}
			}
			else if(descriptor[1] == "ITEMDATAFILL")
			{
				if(descriptorsize > 2)
				{
					int datasize = descriptorsize - 2;
					for(int i = 0; i < datasize; i++, offsetIterator++)
					{
						for(int di = i; di < 16; di += datasize)
							blockOffsets[offsetIdx(blockid, di)] = offsetIterator;
					}
					continue;
				}
				
			}
			else if(descriptor[1] == "STAIR")
			{
				if(descriptorsize > 2)
				{
					setOffsetsForID(blockid, offsetIterator, *this); // asc E
					blockOffsets[offsetIdx(blockid, 1)] = ++offsetIterator; // asc W
					blockOffsets[offsetIdx(blockid, 2)] = ++offsetIterator; // asc S
					blockOffsets[offsetIdx(blockid, 3)] = ++offsetIterator; // asc N
					blockOffsets[offsetIdx(blockid, 4)] = ++offsetIterator; // asc E inverted
					blockOffsets[offsetIdx(blockid, 5)] = ++offsetIterator; // asc W inverted
					blockOffsets[offsetIdx(blockid, 6)] = ++offsetIterator; // asc S inverted
					blockOffsets[offsetIdx(blockid, 7)] = ++offsetIterator; // asc N inverted
				}
			}
			else if(descriptor[1] == "FENCE")
			{
				setOffsetsForID(blockid, offsetIterator, *this); // fence post
				offsetIterator += 15; // reserve space for fence connections
			}
			else if(descriptor[1] == "WALLDATA")
			{
				setOffsetsForID(blockid, offsetIterator, *this);
				for(int i = 0; i < descriptorsize - 2; i++, offsetIterator++)
				{
					blockOffsets[offsetIdx(blockid, i)] = offsetIterator; // wall post
					offsetIterator += 17; // reserve space for wall connections for each wall type (per data bit) + plain walls
				}
				continue;
				
			}
			else if(descriptor[1] == "FENCEGATE")
			{
				setOffsetsForID(blockid, offsetIterator, *this); // fence gate EW
				// fence gate NS
				blockOffsets[offsetIdx(blockid, 1)] = blockOffsets[offsetIdx(blockid, 3)] = blockOffsets[offsetIdx(blockid, 5)] = blockOffsets[offsetIdx(blockid, 7)] = ++offsetIterator;
			}
			else if(descriptor[1] == "MUSHROOM")
			{
				setOffsetsForID(blockid, offsetIterator, *this); // pores on all sides
				blockOffsets[offsetIdx(blockid, 15)] = ++offsetIterator; // all stem
				blockOffsets[offsetIdx(blockid, 7)] = blockOffsets[offsetIdx(blockid, 14)] = ++offsetIterator; // all cap
				blockOffsets[offsetIdx(blockid, 1)] = blockOffsets[offsetIdx(blockid, 4)] = ++offsetIterator; // cap @ UWN + UW
				blockOffsets[offsetIdx(blockid, 2)] = blockOffsets[offsetIdx(blockid, 3)] = blockOffsets[offsetIdx(blockid, 5)] = blockOffsets[offsetIdx(blockid, 6)] = ++offsetIterator; // only top visible - cap @ U + UN + UE + UNE
				blockOffsets[offsetIdx(blockid, 8)] = blockOffsets[offsetIdx(blockid, 9)] = ++offsetIterator; // cap @ US + USE
				blockOffsets[offsetIdx(blockid, 10)] = ++offsetIterator; // stem
			}
			else if(descriptor[1] == "CHEST")
			{
				// single chest
				setOffsetsForID(blockid, offsetIterator, *this);
				blockOffsets[offsetIdx(blockid, 2)] = blockOffsets[offsetIdx(blockid, 5)] = offsetIterator; // facing N,E
				blockOffsets[offsetIdx(blockid, 3)] = ++offsetIterator; // facing S
				blockOffsets[offsetIdx(blockid, 4)] = ++offsetIterator; // facing W
				if(descriptorsize == 4) // double chests
				{
					offsetIterator += 8; // reserve space for connected chests variations
				}
			}
			else if(descriptor[1] == "RAIL")
			{
				setOffsetsForID(blockid, offsetIterator, *this); // flat NS
				blockOffsets[offsetIdx(blockid, 1)] = ++offsetIterator; // flat WE
				blockOffsets[offsetIdx(blockid, 2)] = ++offsetIterator; // asc E
				blockOffsets[offsetIdx(blockid, 3)] = ++offsetIterator; // asc W
				blockOffsets[offsetIdx(blockid, 4)] = ++offsetIterator; // asc N
				blockOffsets[offsetIdx(blockid, 5)] = ++offsetIterator; // asc S
				if(descriptorsize == 4) // rail turns
				{
					blockOffsets[offsetIdx(blockid, 6)] = ++offsetIterator; // NW corner (->SE)
					blockOffsets[offsetIdx(blockid, 7)] = ++offsetIterator; // NE corner (->SW)
					blockOffsets[offsetIdx(blockid, 8)] = ++offsetIterator; // SE corner (->NW)
					blockOffsets[offsetIdx(blockid, 9)] = ++offsetIterator; // SW corner (->NE)
				}
			}
			else if(descriptor[1] == "RAILPOWERED")
			{
				setOffsetsForID(blockid, offsetIterator, *this); // flat NS
				blockOffsets[offsetIdx(blockid, 1)] = ++offsetIterator; // flat WE
				blockOffsets[offsetIdx(blockid, 2)] = ++offsetIterator; // asc E
				blockOffsets[offsetIdx(blockid, 3)] = ++offsetIterator; // asc W
				blockOffsets[offsetIdx(blockid, 4)] = ++offsetIterator; // asc N
				blockOffsets[offsetIdx(blockid, 5)] = ++offsetIterator; // asc S
				blockOffsets[offsetIdx(blockid, 8)] = ++offsetIterator; // flat NS powered
				blockOffsets[offsetIdx(blockid, 9)] = ++offsetIterator; // flat WE powered
				blockOffsets[offsetIdx(blockid, 10)] = ++offsetIterator; // asc E powered
				blockOffsets[offsetIdx(blockid, 11)] = ++offsetIterator; // asc W powered
				blockOffsets[offsetIdx(blockid, 12)] = ++offsetIterator; // asc N powered
				blockOffsets[offsetIdx(blockid, 13)] = ++offsetIterator; // asc S powered
			}
			else if(descriptor[1] == "PANEDATA")
			{
				setOffsetsForID(blockid, offsetIterator, *this); // basic pane
				for(int i = 0; i < descriptorsize - 2; i++, offsetIterator++)
				{
					blockOffsets[offsetIdx(blockid, i)] = offsetIterator; // basic pane
					offsetIterator += 14; // reserve space for pane connections for each pane type (per data bit)
				}
				continue;
			}
			else if(descriptor[1] == "DOOR")
			{
				setOffsetsForID(blockid, offsetIterator, *this); // door
				offsetIterator += 7; // reserve space for different orientations of top and down door parts
			}
			else if(descriptor[1] == "TRAPDOOR")
			{
				// closed on the bottom half of block
				blockOffsets[offsetIdx(blockid, 0)] = blockOffsets[offsetIdx(blockid, 1)] = blockOffsets[offsetIdx(blockid, 2)] = blockOffsets[offsetIdx(blockid, 3)] = offsetIterator;
				// closed on the top half of block
				blockOffsets[offsetIdx(blockid, 8)] = blockOffsets[offsetIdx(blockid, 9)] = blockOffsets[offsetIdx(blockid, 10)] = blockOffsets[offsetIdx(blockid, 11)] = ++offsetIterator;
				blockOffsets[offsetIdx(blockid, 4)] = blockOffsets[offsetIdx(blockid, 12)] = ++offsetIterator; // attached to S
				blockOffsets[offsetIdx(blockid, 5)] = blockOffsets[offsetIdx(blockid, 13)] = ++offsetIterator; // attached to N
				blockOffsets[offsetIdx(blockid, 6)] = blockOffsets[offsetIdx(blockid, 14)] = ++offsetIterator; // attached to E
				blockOffsets[offsetIdx(blockid, 7)] = blockOffsets[offsetIdx(blockid, 15)] = ++offsetIterator; // attached to W
			}
			else if(descriptor[1] == "TORCH")
			{
				setOffsetsForID(blockid, offsetIterator, *this);
				blockOffsets[offsetIdx(blockid, 1)] = ++offsetIterator; // pointing E
				blockOffsets[offsetIdx(blockid, 2)] = ++offsetIterator; // pointing W
				blockOffsets[offsetIdx(blockid, 3)] = ++offsetIterator; // pointing S
				blockOffsets[offsetIdx(blockid, 4)] = ++offsetIterator; // pointing N
			}
			else if(descriptor[1] == "ONWALLPARTIALFILL")
			{
				if(descriptorsize == 11)
				{
					int64_t dataoffset;
					for(int i = 0; i < 4; i++, offsetIterator++) // for each wall side
					{
						if(!fromstring(descriptor[2 + i], dataoffset))
							dataoffset = i;
						for(int di = dataoffset; di < 16; di += 4) // fill until end of blockdata
							blockOffsets[offsetIdx(blockid, di)] = offsetIterator;
					}
					continue;
				}
			}
			else if(descriptor[1] == "WIRE")
			{
				setOffsetsForID(blockid, offsetIterator, *this);
				offsetIterator += 11; // reserve space for different wire connections
			}
			else if(descriptor[1] == "BITANCHOR")
			{
				for(int i = 0; i < 16; i++, offsetIterator++)
					blockOffsets[offsetIdx(blockid, i)] = offsetIterator;
				continue;
			}
			else if(descriptor[1] == "STEM")
			{
				setOffsetsForID(blockid, offsetIterator, *this);
				for(int i = 0; i < 8; i++, offsetIterator++)
					blockOffsets[offsetIdx(blockid, i)] = offsetIterator;
				offsetIterator += 3; // reserve space for different stem connections (N S W E)
			}
			else if(descriptor[1] == "REPEATER")
			{
				blockOffsets[offsetIdx(blockid, 0)] = blockOffsets[offsetIdx(blockid, 4)] = blockOffsets[offsetIdx(blockid, 8)] = blockOffsets[offsetIdx(blockid, 12)] = offsetIterator;
				blockOffsets[offsetIdx(blockid, 1)] = blockOffsets[offsetIdx(blockid, 5)] = blockOffsets[offsetIdx(blockid, 9)] = blockOffsets[offsetIdx(blockid, 13)] = ++offsetIterator;
				blockOffsets[offsetIdx(blockid, 2)] = blockOffsets[offsetIdx(blockid, 6)] = blockOffsets[offsetIdx(blockid, 10)] = blockOffsets[offsetIdx(blockid, 14)] = ++offsetIterator;
				blockOffsets[offsetIdx(blockid, 3)] = blockOffsets[offsetIdx(blockid, 7)] = blockOffsets[offsetIdx(blockid, 11)] = blockOffsets[offsetIdx(blockid, 15)] = ++offsetIterator;
			}
			else if(descriptor[1] == "LEVER")
			{
				blockOffsets[offsetIdx(blockid, 1)] = blockOffsets[offsetIdx(blockid, 9)] = offsetIterator; // facing E
				blockOffsets[offsetIdx(blockid, 2)] = blockOffsets[offsetIdx(blockid, 10)] = ++offsetIterator; // facing W
				blockOffsets[offsetIdx(blockid, 3)] = blockOffsets[offsetIdx(blockid, 11)] = ++offsetIterator; // facing S
				blockOffsets[offsetIdx(blockid, 4)] = blockOffsets[offsetIdx(blockid, 12)] = ++offsetIterator; // facing N
				blockOffsets[offsetIdx(blockid, 5)] = blockOffsets[offsetIdx(blockid, 13)] = ++offsetIterator; // ground NS
				blockOffsets[offsetIdx(blockid, 6)] = blockOffsets[offsetIdx(blockid, 14)] = ++offsetIterator; // ground WE
				blockOffsets[offsetIdx(blockid, 0)] = blockOffsets[offsetIdx(blockid, 7)] = ++offsetIterator; // ceil NS
				blockOffsets[offsetIdx(blockid, 8)] = blockOffsets[offsetIdx(blockid, 15)] = offsetIterator; // ceil WE
			}
			else if(blockid == 8) // water
			{
				setOffsetsForID(8, offsetIterator, *this);
				setOffsetsForID(9, offsetIterator, *this);
				offsetIterator += 3; // reserve space for water special cases (surface, missing W face, missing S face)
				blockOffsets[offsetIdx(8, 1)] = blockOffsets[offsetIdx(9, 1)] = ++offsetIterator; // draw water levels
				blockOffsets[offsetIdx(8, 2)] = blockOffsets[offsetIdx(9, 2)] = ++offsetIterator;
				blockOffsets[offsetIdx(8, 3)] = blockOffsets[offsetIdx(9, 3)] = ++offsetIterator;
				blockOffsets[offsetIdx(8, 4)] = blockOffsets[offsetIdx(9, 4)] = ++offsetIterator;
				blockOffsets[offsetIdx(8, 5)] = blockOffsets[offsetIdx(9, 5)] = ++offsetIterator;
				blockOffsets[offsetIdx(8, 6)] = blockOffsets[offsetIdx(9, 6)] = ++offsetIterator;
				blockOffsets[offsetIdx(8, 7)] = blockOffsets[offsetIdx(9, 7)] = ++offsetIterator;
			}
			else if(blockid == 10) // lava
			{
				setOffsetsForID(10, offsetIterator, *this);
				setOffsetsForID(11, offsetIterator, *this);
				blockOffsets[offsetIdx(10, 2)] = blockOffsets[offsetIdx(11, 2)] = ++offsetIterator;
				blockOffsets[offsetIdx(10, 4)] = blockOffsets[offsetIdx(11, 4)] = ++offsetIterator;
				blockOffsets[offsetIdx(10, 6)] = blockOffsets[offsetIdx(11, 6)] = ++offsetIterator;
			}
			else if(blockid == 26) // bed
			{
				blockOffsets[offsetIdx(blockid, 0)] = blockOffsets[offsetIdx(blockid, 4)] = offsetIterator; // bed foot pointing S
				blockOffsets[offsetIdx(blockid, 1)] = blockOffsets[offsetIdx(blockid, 5)] = ++offsetIterator; // bed foot pointing W
				blockOffsets[offsetIdx(blockid, 2)] = blockOffsets[offsetIdx(blockid, 6)] = ++offsetIterator; // bed foot pointing N
				blockOffsets[offsetIdx(blockid, 3)] = blockOffsets[offsetIdx(blockid, 7)] = ++offsetIterator; // bed foot pointing E
				blockOffsets[offsetIdx(blockid, 8)] = blockOffsets[offsetIdx(blockid, 12)] = ++offsetIterator; // bed head pointing S
				blockOffsets[offsetIdx(blockid, 9)] = blockOffsets[offsetIdx(blockid, 13)] = ++offsetIterator; // bed head pointing W
				blockOffsets[offsetIdx(blockid, 10)] = blockOffsets[offsetIdx(blockid, 14)] = ++offsetIterator; // bed head pointing N
				blockOffsets[offsetIdx(blockid, 11)] = blockOffsets[offsetIdx(blockid, 15)] = ++offsetIterator; // bed head pointing E
			}
			else if(blockid == 117 || blockid == 122 || blockid == 138 || blockid == 154 || descriptor[1] == "SIGNPOST") // single special blocks: brewing stand, dragon egg, beacon, hopper
			{
				setOffsetsForID(blockid, offsetIterator, *this);
			}
			else if(blockid == 118) // cauldron
			{
				setOffsetsForID(blockid, offsetIterator, *this);
				blockOffsets[offsetIdx(blockid, 1)] = ++offsetIterator; // cauldron 1/3 full
				blockOffsets[offsetIdx(blockid, 2)] = ++offsetIterator; // cauldron 2/3 full
				blockOffsets[offsetIdx(blockid, 3)] = ++offsetIterator; // cauldron full
			}
			else if(blockid == 140) // flower pot
			{
				setOffsetsForID(blockid, offsetIterator, *this);  // flower pot
				for(int i = 0; i < descriptorsize - 4; i++)
					blockOffsets[offsetIdx(blockid, i)] = ++offsetIterator;
			}
			else if(blockid == 145) // anvil
			{
				setOffsetsForID(blockid, offsetIterator, *this);  // anvil NS
				blockOffsets[offsetIdx(blockid, 1)] = blockOffsets[offsetIdx(blockid, 3)] = ++offsetIterator; // anvil EW
				blockOffsets[offsetIdx(blockid, 4)] = blockOffsets[offsetIdx(blockid, 6)] = ++offsetIterator; // slightly damaged anvil NS
				blockOffsets[offsetIdx(blockid, 5)] = blockOffsets[offsetIdx(blockid, 7)] = ++offsetIterator; // slightly damaged anvil EW
				blockOffsets[offsetIdx(blockid, 8)] = blockOffsets[offsetIdx(blockid, 10)] = ++offsetIterator; // very damaged anvil NS
				blockOffsets[offsetIdx(blockid, 9)] = blockOffsets[offsetIdx(blockid, 11)] = ++offsetIterator; // very damaged anvil EW
			}
			offsetIterator++;
		}
	}
	
	return offsetIterator;
}

void BlockImages::checkOpacityAndTransparency(int B)
{
	opacity.clear();
	opacity.resize(blockversion, true);
	transparency.clear();
	transparency.resize(blockversion, true);

	for (int i = 0; i < blockversion; i++)
	{
		ImageRect rect = getRect(i);
		// use the face iterators to examine the N, W, and U faces; any non-100% alpha makes
		//  the block non-opaque, and any non-0% alpha makes the block non-transparent
		int tilesize = 2*B;
		// N face starts at [0,B]
		for (FaceIterator it(rect.x, rect.y + B, 1, tilesize); !it.end; it.advance())
		{
			int a = ALPHA(img(it.x, it.y));
			if (a < 255)
				opacity[i] = false;
			if (a > 0)
				transparency[i] = false;
			if (!opacity[i] && !transparency[i])
				break;
		}
		if (!opacity[i] && !transparency[i])
			continue;
		// W face starts at [2B,2B]
		for (FaceIterator it(rect.x + 2*B, rect.y + 2*B, -1, tilesize); !it.end; it.advance())
		{
			int a = ALPHA(img(it.x, it.y));
			if (a < 255)
				opacity[i] = false;
			if (a > 0)
				transparency[i] = false;
			if (!opacity[i] && !transparency[i])
				break;
		}
		if (!opacity[i] && !transparency[i])
			continue;
		// U face starts at [2B-1,0]
		for (TopFaceIterator it(rect.x + 2*B-1, rect.y, tilesize); !it.end; it.advance())
		{
			int a = ALPHA(img(it.x, it.y));
			if (a < 255)
				opacity[i] = false;
			if (a > 0)
				transparency[i] = false;
			if (!opacity[i] && !transparency[i])
				break;
		}
	}
}

void BlockImages::retouchAlphas(int B)
{
	for (int i = 0; i < blockversion; i++)
	{
		ImageRect rect = getRect(i);
		// use the face iterators to examine the N, W, and U faces; any alpha under 10 is changed
		//  to 0, and any alpha above 245 is changed to 255
		int tilesize = 2*B;
		// N face starts at [0,B]
		for (FaceIterator it(rect.x, rect.y + B, 1, tilesize); !it.end; it.advance())
		{
			int a = ALPHA(img(it.x, it.y));
			if (a < 10)
				setAlpha(img(it.x, it.y), 0);
			else if (a > 245)
				setAlpha(img(it.x, it.y), 255);
		}
		// W face starts at [2B,2B]
		for (FaceIterator it(rect.x + 2*B, rect.y + 2*B, -1, tilesize); !it.end; it.advance())
		{
			int a = ALPHA(img(it.x, it.y));
			if (a < 10)
				setAlpha(img(it.x, it.y), 0);
			else if (a > 245)
				setAlpha(img(it.x, it.y), 255);
		}
		// U face starts at [2B-1,0]
		for (TopFaceIterator it(rect.x + 2*B-1, rect.y, tilesize); !it.end; it.advance())
		{
			int a = ALPHA(img(it.x, it.y));
			if (a < 10)
				setAlpha(img(it.x, it.y), 0);
			else if (a > 245)
				setAlpha(img(it.x, it.y), 255);
		}
	}
}

bool BlockImages::construct(int B, ifstream& texturelist, ifstream& descriptorlist, const string& imgpath)
{
	if (B < 2)
		return false;

	int32_t tileSize = 2*B;
	
	// determine some cutoff values for partial block images: given a particular pixel offset in texture tile--for
	//  example, the end portal frame texture is missing its top 3 (out of 16) pixels--we need to know which pixel
	//  in the resized tile is the first one past that offset
	// ...if the texture tile size isn't a multiple of 16 for some reason, this may break down and be ugly
	int CUTOFFS_16[17];
	for(int i = 0; i < 17; i++) 
	{
		CUTOFFS_16[i] = deinterpolate(i, 16, tileSize);
	}
	
	// Load block textures into hashmap based on their name
	unordered_map<std::string, RGBAImage> blockTextures; // vector holding source textures
	string blocktexturespath = imgpath + "/textures/blocks"; // default path
	string textureline;
	string texturename;
	int32_t textureSize;
	unsigned textureiterator = 0;
	bool missingtextures = false;
	RGBAImage iblockimage;
	
	// Reset file stream
	texturelist.clear();
	texturelist.seekg(0, ios_base::beg);
	// Read list of textures (each texture file on new line)
	while(getline(texturelist, textureline))
	{
		vector<string> textureDirectives;
		// read texture list
		if(textureline.size() > 0 && textureline[0] != '#') 
		{
			istringstream texturelinestream(textureline);
			while (!texturelinestream.eof())
			{
				string texturedirective;
				getline( texturelinestream, texturedirective, ' ' );
				if(texturedirective[0] == '#')
					break;
				if(texturedirective.size() == 0) // skip empty directive
					continue;
				textureDirectives.push_back(texturedirective);
			}
		}
		// process directives
		if(textureDirectives.size() == 2 && textureDirectives[0] == "$") // switch texture directory
		{
			blocktexturespath = imgpath + textureDirectives[1];
		}
		else if(textureDirectives.size() > 2 && textureDirectives[0] == "/") // process texture with directives
		{
			if(!iblockimage.readPNG(blocktexturespath + "/" + textureDirectives[1])) // texture read error
			{
				cerr << "[texture.list]" << textureiterator + 1 << " - " << blocktexturespath << "/" << textureDirectives[1] << " is missing or invalid" << endl;
				missingtextures = true;
			}
			else // process texture
			{
				// make a tile
				RGBAImage iblocktile;
				iblocktile.create(tileSize, tileSize);
				textureSize = min(iblockimage.w, iblockimage.h); // assume block textures are square, and choose smallest of the sides if they aren't
				resize(iblockimage, ImageRect(0, 0, textureSize, textureSize), iblocktile, ImageRect(0, 0, tileSize, tileSize));
				
				// directive: RENAME DARKEN OFFSET OFFSETTILE EXPAND CROP FLIPX CHEST LCHEST
				texturename = textureDirectives[1];
				for(unsigned i = 2; i < textureDirectives.size(); i++)
				{
					if(textureDirectives[i] == "RENAME") // assign different name to texture
					{
						texturename = textureDirectives[++i];
					}
					else if(textureDirectives[i] == "DARKEN") // make texture darker; can be used for colorizing monochrome textures aswell
					{
						double darkenR = strtod(textureDirectives[++i].c_str(), NULL);
						double darkenG = strtod(textureDirectives[++i].c_str(), NULL);
						double darkenB = strtod(textureDirectives[++i].c_str(), NULL);
						darken(iblocktile, ImageRect(0, 0, tileSize, tileSize), darkenR, darkenG, darkenB);
					}
					else if(textureDirectives[i] == "OFFSET")
					{
						int xOffset = (fromstring(textureDirectives[++i], xOffset)) ? xOffset : 0;
						int yOffset = (fromstring(textureDirectives[++i], yOffset)) ? yOffset : 0;
						int xSign = (xOffset > 0) - (xOffset < 0);
						int ySign = (yOffset > 0) - (yOffset < 0);
						imgoffset(iblocktile, xSign * CUTOFFS_16[xSign * xOffset%17], ySign * CUTOFFS_16[ySign * yOffset%17]);
					}
					else if(textureDirectives[i] == "OFFSETTILE")
					{
						int xOffset = (fromstring(textureDirectives[++i], xOffset)) ? xOffset : 0;
						int yOffset = (fromstring(textureDirectives[++i], yOffset)) ? yOffset : 0;
						int xSign = (xOffset > 0) - (xOffset < 0);
						int ySign = (yOffset > 0) - (yOffset < 0);
						imgtileoffset(iblocktile, xSign * CUTOFFS_16[xSign * xOffset%17], ySign * CUTOFFS_16[ySign * yOffset%17]);
					}
					else if(textureDirectives[i] == "EXPAND")
					{
						int xExpansion = (fromstring(textureDirectives[++i], xExpansion)) ? xExpansion % 17 : 0;
						int yExpansion = (fromstring(textureDirectives[++i], yExpansion)) ? yExpansion % 17 : 0;
						resize(iblockimage, ImageRect(CUTOFFS_16[xExpansion], CUTOFFS_16[yExpansion], iblockimage.w - 2 * CUTOFFS_16[xExpansion], iblockimage.h - 2 * CUTOFFS_16[yExpansion]), iblocktile, ImageRect(0, 0, tileSize, tileSize));
					}
					else if(textureDirectives[i] == "CROP")
					{
						int tCrop = (fromstring(textureDirectives[++i], tCrop)) ? tCrop % 17 : 0;
						int rCrop = (fromstring(textureDirectives[++i], rCrop)) ? rCrop % 17 : 0;
						int bCrop = (fromstring(textureDirectives[++i], bCrop)) ? bCrop % 17 : 0;
						int lCrop = (fromstring(textureDirectives[++i], lCrop)) ? lCrop % 17 : 0;
						imgcrop(iblocktile, ImageRect(CUTOFFS_16[lCrop], CUTOFFS_16[tCrop], tileSize - (CUTOFFS_16[lCrop] + CUTOFFS_16[rCrop]), tileSize - (CUTOFFS_16[tCrop] + CUTOFFS_16[bCrop])));
					}
					else if(textureDirectives[i] == "FLIPX")
					{
						flipX(iblocktile, ImageRect(0, 0, tileSize, tileSize));
					}
					else if(textureDirectives[i] == "CHEST")
					{
						textureiterator += generateChestTiles(blockTextures, iblockimage, texturename, B);
					}
					else if(textureDirectives[i] == "LCHEST")
					{
						textureiterator += generateLargeChestTiles(blockTextures, iblockimage, texturename, B);
					}
				}
				blockTextures[texturename] = iblocktile;
			}
		}
		else if(textureDirectives.size() == 1) // just load the texture
		{
			if(!iblockimage.readPNG(blocktexturespath + "/" + textureDirectives[0])) // texture read error
			{
				cerr << "[texture.list]" << textureiterator + 1 << " - " << blocktexturespath << "/" << textureDirectives[0] << " is missing or invalid" << endl;
				missingtextures = true;
			}
			else
			{
				texturename = textureDirectives[0];
				RGBAImage iblocktile;
				iblocktile.create(tileSize, tileSize);
				textureSize = min(iblockimage.w, iblockimage.h); // assume block textures are square, and choose smallest of the sides if they aren't
				resize(iblockimage, ImageRect(0, 0, textureSize, textureSize), iblocktile, ImageRect(0, 0, tileSize, tileSize));
				blockTextures[texturename] = iblocktile;
			}
		}
		textureiterator++;
	}
	texturelist.close();
	if(missingtextures)
		return false;
	
	RGBAImage emptyTile;
	emptyTile.create(tileSize, tileSize); // create empty/dummy texture
	blockTextures["/.png"] = emptyTile;
	
	// initialize image
	img.create(rectsize * 16, (blockversion/16 + 1) * rectsize);
	
	// build all block images based on block descriptors
	// Fill in offset depending on block type
	unsigned offsetIterator = 1;
	int64_t blockid;
	for(vector< vector<string> >::iterator it = blockDescriptors.begin(); it != blockDescriptors.end(); ++it)
	{
		vector<string> descriptor = (*it);
		if(fromstring(descriptor[0], blockid))
		{
			int descriptorsize = descriptor.size();
			if(descriptor[1] == "SOLID")
			{
				if(descriptorsize == 3)
				{
					// all faces have the same texture
					RGBAImage& facetexture = blockTextures.at((descriptor[2] + ".png"));
					drawBlockImage(img, getRect(offsetIterator), facetexture, facetexture, facetexture, B);
				}
				else if(descriptorsize == 5)
				{
					// separate textures for separate faces
					drawBlockImage(img, getRect(offsetIterator), blockTextures.at((descriptor[2] + ".png")), blockTextures.at((descriptor[3] + ".png")), blockTextures.at((descriptor[4] + ".png")), B);
				}
			}
			else if(descriptor[1] == "SOLIDORIENTED")
			{
				if(descriptorsize == 9) 
				{
					// texture order: face side top - descriptor[6 7 8]
					drawBlockImage(img, getRect(offsetIterator), blockTextures.at((descriptor[7] + ".png")), blockTextures.at((descriptor[7] + ".png")), blockTextures.at((descriptor[8] + ".png")), B); // this will server for both N and E orientations
					drawBlockImage(img, getRect(++offsetIterator), blockTextures.at((descriptor[7] + ".png")), blockTextures.at((descriptor[6] + ".png")), blockTextures.at((descriptor[8] + ".png")), B);
					drawBlockImage(img, getRect(++offsetIterator), blockTextures.at((descriptor[6] + ".png")), blockTextures.at((descriptor[7] + ".png")), blockTextures.at((descriptor[8] + ".png")), B);
				}
			}
			else if(descriptor[1] == "SOLIDROTATED")
			{
				if(descriptorsize == 5) 
				{
					// piston value order: down / top / N / S / W / E
					// piston extension bit: 0x8 (top bit)
					// texture order: top side bottom - descriptor[2 3 4]
					RGBAImage& topface = blockTextures.at((descriptor[2] + ".png"));
					RGBAImage& sideface = blockTextures.at((descriptor[3] + ".png"));
					RGBAImage& bottomface = blockTextures.at((descriptor[4] + ".png"));
					drawRotatedBlockImage(img, getRect(offsetIterator), blockTile(sideface, 2, false), blockTile(sideface, 2, false), blockTile(bottomface), B);  // facing Down
					drawRotatedBlockImage(img, getRect(++offsetIterator), blockTile(sideface), blockTile(sideface), blockTile(topface), B);  // facing Up
					drawRotatedBlockImage(img, getRect(++offsetIterator), blockTile(sideface, 1, false), blockTile(bottomface), blockTile(sideface, 1, false), B);  // facing N
					drawRotatedBlockImage(img, getRect(++offsetIterator), blockTile(sideface, 3, false), blockTile(topface), blockTile(sideface, 3, false), B);  // facing S
					drawRotatedBlockImage(img, getRect(++offsetIterator), blockTile(topface), blockTile(sideface, 1, false), blockTile(sideface, 2, false), B);  // facing W
					drawRotatedBlockImage(img, getRect(++offsetIterator), blockTile(bottomface), blockTile(sideface, 3, false), blockTile(sideface), B);  // facing E
				}
			}
			else if(descriptor[1] == "SOLIDDATA" || descriptor[1] == "SOLIDDATAFILL")
			{
				if(descriptorsize > 2)
				{
					for(int i = 2; i < descriptorsize; i++, offsetIterator++)
						drawBlockImage(img, getRect(offsetIterator), blockTextures.at((descriptor[i] + ".png")), blockTextures.at((descriptor[i] + ".png")), blockTextures.at((descriptor[i] + ".png")), B);
					continue;
				}
			}
			else if(descriptor[1] == "SOLIDDATATRUNK")
			{
				if(descriptorsize % 2 == 0 && descriptorsize > 3)
				{
					for(int i = 2; i < descriptorsize; i += 2, offsetIterator++)
						drawBlockImage(img, getRect(offsetIterator), blockTextures.at((descriptor[i+1] + ".png")), blockTextures.at((descriptor[i+1] + ".png")), blockTextures.at((descriptor[i] + ".png")), B);
					continue;
				}
			}
			else if(descriptor[1] == "SOLIDDATATRUNKROTATED")
			{
				if((descriptorsize - 3) % 3 == 0 && descriptorsize > 3)
				{
					int orientationFlag;
					
					RGBAImage trunkTop;
					RGBAImage trunkSide;
					for(int i = 0; i < descriptorsize - 3; i += 3, offsetIterator++)
					{
						trunkTop = blockTextures.at((descriptor[i+4] + ".png"));
						trunkSide = blockTextures.at((descriptor[i+5] + ".png"));
						drawBlockImage(img, getRect(offsetIterator), trunkSide, trunkSide, trunkTop, B); // trunk UD
						
						if(!fromstring(descriptor[i + 3], orientationFlag))
							orientationFlag = 0;
						if(orientationFlag == 1)
						{
							drawRotatedBlockImage(img, getRect(++offsetIterator), blockTile(trunkTop, 0, false), blockTile(trunkSide, 3, false), blockTile(trunkSide, 0, false), B);  // trunk EW
							drawRotatedBlockImage(img, getRect(++offsetIterator), blockTile(trunkSide, 1, false), blockTile(trunkTop, 0, false), blockTile(trunkSide, 1, false), B);  // trunk NS
						}
					}
					continue;
				}
			}
			else if(descriptor[1] == "SOLIDOBSTRUCTED")
			{
				if(descriptorsize == 3)
				{
					// all faces have the same texture
					RGBAImage& facetexture = blockTextures.at((descriptor[2] + ".png"));
					drawBlockImage(img, getRect(offsetIterator), facetexture, facetexture, facetexture, B);
					drawRotatedBlockImage(img, getRect(++offsetIterator), SourceTile(), SourceTile(), blockTile(facetexture), B);  // only top surface
					drawRotatedBlockImage(img, getRect(++offsetIterator), SourceTile(), blockTile(facetexture), blockTile(facetexture), B);  // missing W
					drawRotatedBlockImage(img, getRect(++offsetIterator), blockTile(facetexture), SourceTile(), blockTile(facetexture), B);  // missing S
				}
				else if(descriptorsize == 5)
				{
					// separate textures for separate faces
					RGBAImage& wtexture = blockTextures.at((descriptor[2] + ".png"));
					RGBAImage& stexture = blockTextures.at((descriptor[3] + ".png"));
					RGBAImage& toptexture = blockTextures.at((descriptor[4] + ".png"));
					drawBlockImage(img, getRect(offsetIterator), wtexture, stexture, toptexture, B);
					drawRotatedBlockImage(img, getRect(++offsetIterator), SourceTile(), blockTile(stexture), blockTile(toptexture), B);  // missing W
					drawRotatedBlockImage(img, getRect(++offsetIterator), blockTile(wtexture), SourceTile(), blockTile(toptexture), B);  // missing S
					drawRotatedBlockImage(img, getRect(++offsetIterator), SourceTile(), SourceTile(), blockTile(toptexture), B);  // only top surface
				}
			}
			else if(descriptor[1] == "SOLIDPARTIAL")
			{
				int topCutoff;
				int bottomCutoff;
				if(fromstring(descriptor[2], topCutoff) && fromstring(descriptor[3], bottomCutoff) && descriptorsize == 7) {
					topCutoff = min(max(topCutoff, 0), 16);
					bottomCutoff = min(max(bottomCutoff, 0), 16);
					drawPartialBlockImage(img, getRect(offsetIterator), blockTextures.at((descriptor[4] + ".png")), blockTextures.at((descriptor[5] + ".png")), blockTextures.at((descriptor[6] + ".png")), B, CUTOFFS_16[topCutoff], CUTOFFS_16[bottomCutoff], 0, 0, false);
				}
			}
			else if(descriptor[1] == "SOLIDDATAPARTIALFILL")
			{
				int datasize = descriptorsize - 5;
				if(datasize % 2 == 0)
				{
					int topCutoff;
					int bottomCutoff;
					RGBAImage& Wface = blockTextures.at((descriptor[2] + ".png"));
					RGBAImage& Sface = blockTextures.at((descriptor[3] + ".png"));
					RGBAImage& Uface = blockTextures.at((descriptor[4] + ".png"));
					for(int i = 0; i < datasize; i += 2, offsetIterator++)
					{
						if(!fromstring(descriptor[i+5], topCutoff)) topCutoff = 0;
						if(!fromstring(descriptor[i+6], bottomCutoff)) bottomCutoff = 0;
						drawPartialBlockImage(img, getRect(offsetIterator), Wface, Sface, Uface, B, CUTOFFS_16[topCutoff], CUTOFFS_16[bottomCutoff], 0, 0, false);
					}
					continue;
				}
			}
			else if(descriptor[1] == "SOLIDTRANSPARENT")
			{
				if(descriptorsize == 8)
				{
					RGBAImage& Dface = blockTextures.at((descriptor[2] + ".png"));
					RGBAImage& Nface = blockTextures.at((descriptor[3] + ".png"));
					RGBAImage& Eface = blockTextures.at((descriptor[4] + ".png"));
					RGBAImage& Wface = blockTextures.at((descriptor[5] + ".png"));
					RGBAImage& Sface = blockTextures.at((descriptor[6] + ".png"));
					RGBAImage& Uface = blockTextures.at((descriptor[7] + ".png"));
					
					drawFloorBlockImage(img, getRect(offsetIterator), Dface, 0, B);
					drawSingleFaceBlockImage(img, getRect(offsetIterator), Nface, 3, B);
					drawSingleFaceBlockImage(img, getRect(offsetIterator), Eface, 0, B);
					drawSingleFaceBlockImage(img, getRect(offsetIterator), Wface, 1, B);
					drawSingleFaceBlockImage(img, getRect(offsetIterator), Sface, 2, B);
					drawOffsetPaddedUFace(img, getRect(offsetIterator), Uface, B, 0, 0, 0, 0, 0);
					//drawRotatedBlockImage(img, getRect(offsetIterator), SourceTile(), SourceTile(), blockTile(Uface), B); // closed on the top half of block
				}
			}
			else if(descriptor[1] == "SLABDATA")
			{
				if(descriptorsize > 2)
				{
					for(int i = 2; i < descriptorsize; i++, offsetIterator++)
					{
						// bottom slab
						drawPartialBlockImage(img, getRect(offsetIterator), blockTextures.at((descriptor[i] + ".png")), blockTextures.at((descriptor[i] + ".png")), blockTextures.at((descriptor[i] + ".png")), B, CUTOFFS_16[8], 0, 0, 0, true);
						// inverted slab
						drawPartialBlockImage(img, getRect(++offsetIterator), blockTextures.at((descriptor[i] + ".png")), blockTextures.at((descriptor[i] + ".png")), blockTextures.at((descriptor[i] + ".png")), B, 0, CUTOFFS_16[8], 0, 0, false);
					}
					continue;
				}
			}
			else if(descriptor[1] == "SLABDATATRUNK")
			{
				if(descriptorsize % 2 == 0 && descriptorsize > 3)
				{
					for(int i = 2; i < descriptorsize; i += 2, offsetIterator++)
					{
						// bottom slab
						drawPartialBlockImage(img, getRect(offsetIterator), blockTextures.at((descriptor[i+1] + ".png")), blockTextures.at((descriptor[i+1] + ".png")), blockTextures.at((descriptor[i] + ".png")), B, CUTOFFS_16[8], 0, 0, 0, true);
						// inverted slab
						drawPartialBlockImage(img, getRect(++offsetIterator), blockTextures.at((descriptor[i+1] + ".png")), blockTextures.at((descriptor[i+1] + ".png")), blockTextures.at((descriptor[i] + ".png")), B, 0, CUTOFFS_16[8], 0, 0, false);
					}
					continue;
				}
			}
			else if(descriptor[1] == "ITEMDATA" || descriptor[1] == "ITEMDATAFILL")
			{
				if(descriptorsize > 2)
				{
					for(int i = 2; i < descriptorsize; i++, offsetIterator++)
					{
						drawItemBlockImage(img, getRect(offsetIterator), blockTextures.at((descriptor[i] + ".png")), B);
					}
					continue;
				}
			}
			else if(descriptor[1] == "ITEMDATAORIENTED")
			{
				if(descriptorsize > 6)
				{
					int datasize = descriptorsize - 6;
					// orientation bits order: N S W E - descriptor[2 3 4 5]
					RGBAImage baseTile;
					for(int i = 0; i < datasize; i++, offsetIterator++)
					{
						baseTile = blockTextures[descriptor[i + 6] + ".png"];
						drawPartialItemBlockImage(img, getRect(offsetIterator), baseTile, 0, false, false, true, false, false, B);  // attached to N (S side)
						drawPartialItemBlockImage(img, getRect(++offsetIterator), baseTile, 0, true, true, false, false, false, B);  // attached S (N side)
						drawPartialItemBlockImage(img, getRect(++offsetIterator), baseTile, 0, false, false, false, false, true, B);  // attached W (E side)
						drawPartialItemBlockImage(img, getRect(++offsetIterator), baseTile, 0, true, false, false, true, false, B);  // attached E (W side)
					}
					continue;
				}
			}
			else if(descriptor[1] == "MULTIITEMDATA")
			{
				if(descriptorsize > 2)
				{
					for(int i = 2; i < descriptorsize; i++, offsetIterator++)
					{
						drawMultiItemBlockImage(img, getRect(offsetIterator), blockTextures.at((descriptor[i] + ".png")), B);
					}
					continue;
				}
			}
			else if(descriptor[1] == "STAIR")
			{
				if(descriptorsize > 2)
					{
					RGBAImage sideface;
					RGBAImage topface;
					if(descriptorsize == 3)
					{
						sideface = topface = blockTextures.at((descriptor[2] + ".png"));
						
					}
					else if(descriptorsize > 3)
					{
						sideface = blockTextures.at((descriptor[2] + ".png"));
						topface = blockTextures.at((descriptor[3] + ".png"));
						
					}
					drawStairsE(img, getRect(offsetIterator), sideface, topface, B);  // stairs asc E
					drawStairsW(img, getRect(++offsetIterator), sideface, topface, B);  // stairs asc W
					drawStairsS(img, getRect(++offsetIterator), sideface, topface, B);  // stairs asc S
					drawStairsN(img, getRect(++offsetIterator), sideface, topface, B);  // stairs asc N
					drawInvStairsE(img, getRect(++offsetIterator), sideface, topface, B);  // stairs asc E inverted
					drawInvStairsW(img, getRect(++offsetIterator), sideface, topface, B);  // stairs asc W inverted
					drawInvStairsS(img, getRect(++offsetIterator), sideface, topface, B);  // stairs asc S inverted
					drawInvStairsN(img, getRect(++offsetIterator), sideface, topface, B);  // stairs asc N inverted
				}
			}
			else if(descriptor[1] == "FENCE")
			{
				RGBAImage& baseTexture = blockTextures.at((descriptor[2] + ".png"));
				drawFencePost(img, getRect(offsetIterator), baseTexture, B);  // fence post
				drawFence(img, getRect(++offsetIterator), baseTexture, true, false, false, false, true, B);  // fence N
				drawFence(img, getRect(++offsetIterator), baseTexture, false, true, false, false, true, B);  // fence S
				drawFence(img, getRect(++offsetIterator), baseTexture, true, true, false, false, true, B);  // fence NS
				drawFence(img, getRect(++offsetIterator), baseTexture, false, false, false, true, true, B);  // fence E
				drawFence(img, getRect(++offsetIterator), baseTexture, true, false, false, true, true, B);  // fence NE
				drawFence(img, getRect(++offsetIterator), baseTexture, false, true, false, true, true, B);  // fence SE
				drawFence(img, getRect(++offsetIterator), baseTexture, true, true, false, true, true, B);  // fence NSE
				drawFence(img, getRect(++offsetIterator), baseTexture, false, false, true, false, true, B);  // fence W
				drawFence(img, getRect(++offsetIterator), baseTexture, true, false, true, false, true, B);  // fence NW
				drawFence(img, getRect(++offsetIterator), baseTexture, false, true, true, false, true, B);  // fence SW
				drawFence(img, getRect(++offsetIterator), baseTexture, true, true, true, false, true, B);  // fence NSW
				drawFence(img, getRect(++offsetIterator), baseTexture, false, false, true, true, true, B);  // fence EW
				drawFence(img, getRect(++offsetIterator), baseTexture, true, false, true, true, true, B);  // fence NEW
				drawFence(img, getRect(++offsetIterator), baseTexture, false, true, true, true, true, B);  // fence SEW
				drawFence(img, getRect(++offsetIterator), baseTexture, true, true, true, true, true, B);  // fence NSEW
			}
			else if(descriptor[1] == "WALLDATA")
			{
				RGBAImage baseTexture;
				for(int i = 0; i < descriptorsize - 2; i++, offsetIterator++)
				{
					baseTexture = blockTextures.at((descriptor[2 + i] + ".png"));
					drawStoneWallPost(img, getRect(offsetIterator), baseTexture, B); // cobblestone wall post
					drawStoneWallConnected(img, getRect(++offsetIterator), baseTexture, true, false, false, false, B);  // cobblestone wall post N
					drawStoneWallConnected(img, getRect(++offsetIterator), baseTexture, false, true, false, false, B);  // cobblestone wall post S
					drawStoneWallConnected(img, getRect(++offsetIterator), baseTexture, true, true, false, false, B);  // cobblestone wall post NS
					drawStoneWallConnected(img, getRect(++offsetIterator), baseTexture, false, false, true, false, B);  // cobblestone wall post E
					drawStoneWallConnected(img, getRect(++offsetIterator), baseTexture, true, false, true, false, B);  // cobblestone wall post NE
					drawStoneWallConnected(img, getRect(++offsetIterator), baseTexture, false, true, true, false, B);  // cobblestone wall post SE
					drawStoneWallConnected(img, getRect(++offsetIterator), baseTexture, true, true, true, false, B);  // cobblestone wall post NSE
					drawStoneWallConnected(img, getRect(++offsetIterator), baseTexture, false, false, false, true, B);  // cobblestone wall post W
					drawStoneWallConnected(img, getRect(++offsetIterator), baseTexture, true, false, false, true, B);  // cobblestone wall post NW
					drawStoneWallConnected(img, getRect(++offsetIterator), baseTexture, false, true, false, true, B);  // cobblestone wall post SW
					drawStoneWallConnected(img, getRect(++offsetIterator), baseTexture, true, true, false, true, B);  // cobblestone wall post NSW
					drawStoneWallConnected(img, getRect(++offsetIterator), baseTexture, false, false, true, true, B);  // cobblestone wall post EW
					drawStoneWallConnected(img, getRect(++offsetIterator), baseTexture, true, false, true, true, B);  // cobblestone wall post NEW
					drawStoneWallConnected(img, getRect(++offsetIterator), baseTexture, false, true, true, true, B);  // cobblestone wall post SEW
					drawStoneWallConnected(img, getRect(++offsetIterator), baseTexture, true, true, true, true, B);  // cobblestone wall post NSEW
					drawStoneWall(img, getRect(++offsetIterator), baseTexture, true, B); // cobblestone wall NS
					drawStoneWall(img, getRect(++offsetIterator), baseTexture, false, B); // cobblestone wall EW
				}
				continue;
			}
			else if(descriptor[1] == "FENCEGATE")
			{
				RGBAImage& baseTile = blockTextures.at((descriptor[2] + ".png"));
				drawOffsetPaddedSFace(img, getRect(offsetIterator), baseTile, B, 0.8, CUTOFFS_16[8], CUTOFFS_16[4], CUTOFFS_16[4], 0, 0); // fence gate EW
				drawOffsetPaddedWFace(img, getRect(++offsetIterator), baseTile, B, 0.9, CUTOFFS_16[8], CUTOFFS_16[4], CUTOFFS_16[4], 0, 0); // fence gate NS
			}
			else if(descriptor[1] == "MUSHROOM")
			{
				RGBAImage& poreTile = blockTextures.at((descriptor[2] + ".png"));
				RGBAImage& stemTile = blockTextures.at((descriptor[3] + ".png"));
				RGBAImage& capTile = blockTextures.at((descriptor[4] + ".png"));
				drawBlockImage(img, getRect(offsetIterator), poreTile, poreTile, poreTile, B); // pores on all sides
				drawBlockImage(img, getRect(++offsetIterator), stemTile, stemTile, stemTile, B); // all stem
				drawBlockImage(img, getRect(++offsetIterator), capTile, capTile, capTile, B); // all cap
				drawBlockImage(img, getRect(++offsetIterator), capTile, poreTile, capTile, B); // cap @ UWN + UW
				drawBlockImage(img, getRect(++offsetIterator), poreTile, poreTile, capTile, B); // only top - cap @ U + UN + UE + UNE
				drawBlockImage(img, getRect(++offsetIterator), poreTile, capTile, capTile, B); // cap @ US + USE
				drawBlockImage(img, getRect(++offsetIterator), stemTile, stemTile, poreTile, B); // stem
			}
			else if(descriptor[1] == "CHEST")
			{
				// single chest
				RGBAImage& chestTop = blockTextures.at((descriptor[2] + ".png_0"));
				RGBAImage& chestFront = blockTextures.at((descriptor[2] + ".png_1"));
				RGBAImage& chestSide = blockTextures.at((descriptor[2] + ".png_2"));
				drawBlockImage(img, getRect(offsetIterator), chestSide, chestSide, chestTop, B);  // facing N,E
				drawBlockImage(img, getRect(++offsetIterator), chestSide, chestFront, chestTop, B);  // facing S
				drawBlockImage(img, getRect(++offsetIterator), chestFront, chestSide, chestTop, B);  // facing W
				if(descriptorsize == 4) // double chests
				{
					RGBAImage largeTile0 = blockTextures.at((descriptor[3] + ".png_0"));
					RGBAImage largeTile1 = blockTextures.at((descriptor[3] + ".png_1"));
					RGBAImage largeTile2 = blockTextures.at((descriptor[3] + ".png_2"));
					RGBAImage largeTile3 = blockTextures.at((descriptor[3] + ".png_3"));
					RGBAImage largeTile4 = blockTextures.at((descriptor[3] + ".png_4"));
					RGBAImage largeTile5 = blockTextures.at((descriptor[3] + ".png_5"));
					RGBAImage largeTile6 = blockTextures.at((descriptor[3] + ".png_6"));
					drawRotatedBlockImage(img, getRect(++offsetIterator), blockTile(largeTile6, 0, false), blockTile(largeTile4, 0, false), blockTile(largeTile0, 1, false), B);  // double chest W facing N
					drawRotatedBlockImage(img, getRect(++offsetIterator), blockTile(largeTile6, 0, false), blockTile(largeTile5, 0, false), blockTile(largeTile1, 1, false), B);  // double chest E facing N
					drawRotatedBlockImage(img, getRect(++offsetIterator), blockTile(largeTile6, 0, false), blockTile(largeTile2, 0, false), blockTile(largeTile0, 1, false), B);  // double chest W facing S
					drawRotatedBlockImage(img, getRect(++offsetIterator), blockTile(largeTile6, 0, false), blockTile(largeTile3, 0, false), blockTile(largeTile1, 1, false), B);  // double chest E facing S
					drawBlockImage(img, getRect(++offsetIterator), largeTile2, largeTile6, largeTile0, B);  // double chest S facing W
					drawBlockImage(img, getRect(++offsetIterator), largeTile3, largeTile6, largeTile1, B);  // double chest N facing W
					drawBlockImage(img, getRect(++offsetIterator), largeTile4, largeTile6, largeTile0, B);  // double chest S facing E
					drawBlockImage(img, getRect(++offsetIterator), largeTile5, largeTile6, largeTile1, B);  // double chest N facing E
				}
			}
			else if(descriptor[1] == "RAIL")
			{
				RGBAImage& rail = blockTextures.at(descriptor[2] + ".png");
				drawFloorBlockImage(img, getRect(offsetIterator), rail, 1, B);  // flat NS
				drawFloorBlockImage(img, getRect(++offsetIterator), rail, 0, B);  // flat EW
				drawAngledFloorBlockImage(img, getRect(++offsetIterator), rail, 0, 0, B);  // asc E
				drawAngledFloorBlockImage(img, getRect(++offsetIterator), rail, 0, 2, B);  // asc W
				drawAngledFloorBlockImage(img, getRect(++offsetIterator), rail, 1, 3, B);  // asc N
				drawAngledFloorBlockImage(img, getRect(++offsetIterator), rail, 1, 1, B);  // asc S
				if(descriptorsize == 4)
				{
					RGBAImage& railcorner = blockTextures.at(descriptor[3] + ".png");
					drawFloorBlockImage(img, getRect(++offsetIterator), railcorner, 1, B);  // track NW corner
					drawFloorBlockImage(img, getRect(++offsetIterator), railcorner, 0, B);  // track NE corner
					drawFloorBlockImage(img, getRect(++offsetIterator), railcorner, 3, B);  // track SE corner
					drawFloorBlockImage(img, getRect(++offsetIterator), railcorner, 2, B);  // track SW corner
				}
			}
			else if(descriptor[1] == "RAILPOWERED")
			{
				RGBAImage& rail = blockTextures.at(descriptor[2] + ".png");
				RGBAImage& railpowered = blockTextures.at(descriptor[3] + ".png");
				drawFloorBlockImage(img, getRect(offsetIterator), rail, 1, B);  // flat NS
				drawFloorBlockImage(img, getRect(++offsetIterator), rail, 0, B);  // flat EW
				drawAngledFloorBlockImage(img, getRect(++offsetIterator), rail, 0, 0, B);  // asc E
				drawAngledFloorBlockImage(img, getRect(++offsetIterator), rail, 0, 2, B);  // asc W
				drawAngledFloorBlockImage(img, getRect(++offsetIterator), rail, 1, 3, B);  // asc N
				drawAngledFloorBlockImage(img, getRect(++offsetIterator), rail, 1, 1, B);  // asc S
				
				drawFloorBlockImage(img, getRect(++offsetIterator), railpowered, 1, B);  // flat NS powered
				drawFloorBlockImage(img, getRect(++offsetIterator), railpowered, 0, B);  // flat EW powered
				drawAngledFloorBlockImage(img, getRect(++offsetIterator), railpowered, 0, 0, B);  // asc E powered
				drawAngledFloorBlockImage(img, getRect(++offsetIterator), railpowered, 0, 2, B);  // asc W powered
				drawAngledFloorBlockImage(img, getRect(++offsetIterator), railpowered, 1, 3, B);  // asc N powered
				drawAngledFloorBlockImage(img, getRect(++offsetIterator), railpowered, 1, 1, B);  // asc S powered
			}
			else if(descriptor[1] == "PANEDATA")
			{
				RGBAImage baseTexture;
				for(int i = 0; i < descriptorsize - 2; i++, offsetIterator++)
				{
					baseTexture = blockTextures.at((descriptor[2 + i] + ".png"));
					drawItemBlockImage(img, getRect(offsetIterator), baseTexture, B);  // base pane unconnected / NSWE
					drawPartialItemBlockImage(img, getRect(++offsetIterator), baseTexture, 0, false, true, false, false, false, B); // pane N
					drawPartialItemBlockImage(img, getRect(++offsetIterator), baseTexture, 0, false, false, true, false, false, B); // pane S
					drawPartialItemBlockImage(img, getRect(++offsetIterator), baseTexture, 0, false, true, true, false, false, B); // pane NS
					drawPartialItemBlockImage(img, getRect(++offsetIterator), baseTexture, 0, false, false, false, false, true, B); // pane E
					drawPartialItemBlockImage(img, getRect(++offsetIterator), baseTexture, 0, false, true, false, false, true, B); // pane NE
					drawPartialItemBlockImage(img, getRect(++offsetIterator), baseTexture, 0, false, false, true, false, true, B); // pane SE
					drawPartialItemBlockImage(img, getRect(++offsetIterator), baseTexture, 0, false, true, true, false, true, B); // pane NSE
					drawPartialItemBlockImage(img, getRect(++offsetIterator), baseTexture, 0, false, false, false, true, false, B); // pane W
					drawPartialItemBlockImage(img, getRect(++offsetIterator), baseTexture, 0, false, true, false, true, false, B); // pane NW
					drawPartialItemBlockImage(img, getRect(++offsetIterator), baseTexture, 0, false, false, true, true, false, B); // pane SW
					drawPartialItemBlockImage(img, getRect(++offsetIterator), baseTexture, 0, false, true, true, true, false, B); // pane NSW
					drawPartialItemBlockImage(img, getRect(++offsetIterator), baseTexture, 0, false, false, false, true, true, B); // pane EW
					drawPartialItemBlockImage(img, getRect(++offsetIterator), baseTexture, 0, false, true, false, true, true, B); // pane NEW
					drawPartialItemBlockImage(img, getRect(++offsetIterator), baseTexture, 0, false, false, true, true, true, B); // pane SEW
				}
				continue;
			}
			else if(descriptor[1] == "DOOR")
			{
				RGBAImage& bottomTile = blockTextures.at((descriptor[2] + ".png"));
				RGBAImage& topTile = blockTextures.at((descriptor[3] + ".png"));
				drawSingleFaceBlockImage(img, getRect(offsetIterator), bottomTile, 1, B);  // door W side
				drawSingleFaceBlockImage(img, getRect(++offsetIterator), bottomTile, 3, B);  // door N side
				drawSingleFaceBlockImage(img, getRect(++offsetIterator), bottomTile, 0, B);  // door E side
				drawSingleFaceBlockImage(img, getRect(++offsetIterator), bottomTile, 2, B);  // door S side
				drawSingleFaceBlockImage(img, getRect(++offsetIterator), topTile, 1, B);  // door W top side
				drawSingleFaceBlockImage(img, getRect(++offsetIterator), topTile, 3, B);  // door N top side
				drawSingleFaceBlockImage(img, getRect(++offsetIterator), topTile, 0, B);  // door E top side
				drawSingleFaceBlockImage(img, getRect(++offsetIterator), topTile, 2, B);  // door S top side
			}
			else if(descriptor[1] == "TRAPDOOR")
			{
				RGBAImage& baseTile = blockTextures.at((descriptor[2] + ".png"));
				
				drawFloorBlockImage(img, getRect(offsetIterator), blockTile(baseTile), B); // closed on the bottom half of block
				drawRotatedBlockImage(img, getRect(++offsetIterator), SourceTile(), SourceTile(), blockTile(baseTile), B); // closed on the top half of block
				drawSingleFaceBlockImage(img, getRect(++offsetIterator), baseTile, 2, B);  // trapdoor open S
				drawSingleFaceBlockImage(img, getRect(++offsetIterator), baseTile, 3, B);  // trapdoor open N
				drawSingleFaceBlockImage(img, getRect(++offsetIterator), baseTile, 0, B);  // trapdoor open E
				drawSingleFaceBlockImage(img, getRect(++offsetIterator), baseTile, 1, B);  // trapdoor open W
			}
			else if(descriptor[1] == "TORCH")
			{
				RGBAImage& torchTile = blockTextures.at((descriptor[2] + ".png"));
				drawItemBlockImage(img, getRect(offsetIterator), torchTile, B);  // torch floor
				drawSingleFaceBlockImage(img, getRect(++offsetIterator), torchTile, 1, B);  // torch pointing E
				drawSingleFaceBlockImage(img, getRect(++offsetIterator), torchTile, 0, B);  // torch pointing W
				drawSingleFaceBlockImage(img, getRect(++offsetIterator), torchTile, 3, B);  // torch pointing S
				drawSingleFaceBlockImage(img, getRect(++offsetIterator), torchTile, 2, B);  // torch pointing N
			}
			else if(descriptor[1] == "ONWALLPARTIALFILL")
			{
				if(descriptorsize == 11)
				{
					RGBAImage faceTile = blockTextures[descriptor[6] + ".png"];
					int croptop;
					int cropbottom;
					int cropleft;
					int cropright;
					if(!fromstring(descriptor[7], croptop)) croptop = 0;
					else croptop = CUTOFFS_16[croptop];
					if(!fromstring(descriptor[8], cropbottom)) cropbottom = 0;
					else cropbottom = CUTOFFS_16[cropbottom];
					if(!fromstring(descriptor[9], cropleft)) cropleft = 0;
					else cropleft = CUTOFFS_16[cropleft];
					if(!fromstring(descriptor[10], cropright)) cropright = 0;
					else cropright = CUTOFFS_16[cropright];
					if((croptop + cropbottom + cropleft + cropright) == 0)
					{
						drawSingleFaceBlockImage(img, getRect(offsetIterator), faceTile, 3, B);  // facing S (N wall)
						drawSingleFaceBlockImage(img, getRect(++offsetIterator), faceTile, 2, B);  // facing N (S wall)
						drawSingleFaceBlockImage(img, getRect(++offsetIterator), faceTile, 1, B);  // facing E (W wall)
						drawSingleFaceBlockImage(img, getRect(++offsetIterator), faceTile, 0, B);  // facing W (E wall)
					}
					else
					{
						drawPartialSingleFaceBlockImage(img, getRect(offsetIterator), faceTile, 3, B, croptop, cropbottom, cropleft, cropright);  // facing S (N wall)
						drawPartialSingleFaceBlockImage(img, getRect(++offsetIterator), faceTile, 2, B, croptop, cropbottom, cropleft, cropright);  // facing N (S wall)
						drawPartialSingleFaceBlockImage(img, getRect(++offsetIterator), faceTile, 1, B, croptop, cropbottom, cropleft, cropright);  // facing E (W wall)
						drawPartialSingleFaceBlockImage(img, getRect(++offsetIterator), faceTile, 0, B, croptop, cropbottom, cropleft, cropright);  // facing W (E wall)
					}
				}
			}
			else if(descriptor[1] == "WIRE")
			{
				//setOffsetsForID(blockid, offsetIterator, *this);
				RGBAImage wireCross;
				RGBAImage& wireTile = blockTextures[descriptor[2] + ".png"];
				if(descriptorsize == 4)
				{
					wireCross = blockTextures[descriptor[3] + ".png"];
					drawOffsetPaddedUFace(img, getRect(offsetIterator), wireCross, B, CUTOFFS_16[16], CUTOFFS_16[5], CUTOFFS_16[5], CUTOFFS_16[5], CUTOFFS_16[5]); // unconnected
				}
				else
				{
					// we will need to create crossed wire
					RGBAImage rotatedTile;
					rotatedTile.create(tileSize, tileSize);
					RotatedFaceIterator dstit(0, 0, 1, tileSize, false);
					for (FaceIterator srcit(0, 0, 0, tileSize); !srcit.end; srcit.advance(), dstit.advance())
						rotatedTile(dstit.x, dstit.y) = wireTile(srcit.x, srcit.y);
					
					wireCross.create(tileSize, tileSize);
					alphablit(wireTile, ImageRect(0, 0, tileSize, tileSize), wireCross, 0, 0);
					alphablit(rotatedTile, ImageRect(0, 0, tileSize, tileSize), wireCross, 0, 0);
					
					drawFloorBlockImage(img, getRect(offsetIterator), wireTile, 0, B);  // unconnected
				}
				drawFloorBlockImage(img, getRect(++offsetIterator), wireTile, 0, B);  // flat NS
				drawFloorBlockImage(img, getRect(++offsetIterator), wireTile, 1, B);  // flat WE
				drawOffsetPaddedUFace(img, getRect(++offsetIterator), wireCross, B, CUTOFFS_16[16], 0, CUTOFFS_16[5], 0, CUTOFFS_16[5]); // NE
				drawOffsetPaddedUFace(img, getRect(++offsetIterator), wireCross, B, CUTOFFS_16[16], 0, CUTOFFS_16[5], CUTOFFS_16[5], 0); // SE
				drawOffsetPaddedUFace(img, getRect(++offsetIterator), wireCross, B, CUTOFFS_16[16], 0, CUTOFFS_16[5], 0, 0); // NSE
				drawOffsetPaddedUFace(img, getRect(++offsetIterator), wireCross, B, CUTOFFS_16[16], CUTOFFS_16[5], 0, 0, CUTOFFS_16[5]); // NW
				drawOffsetPaddedUFace(img, getRect(++offsetIterator), wireCross, B, CUTOFFS_16[16], CUTOFFS_16[5], 0, CUTOFFS_16[5], 0); // SW
				drawOffsetPaddedUFace(img, getRect(++offsetIterator), wireCross, B, CUTOFFS_16[16], CUTOFFS_16[5], 0, 0, 0); // NSW
				drawOffsetPaddedUFace(img, getRect(++offsetIterator), wireCross, B, CUTOFFS_16[16], 0, 0, 0, CUTOFFS_16[5]); // NEW
				drawOffsetPaddedUFace(img, getRect(++offsetIterator), wireCross, B, CUTOFFS_16[16], 0, 0, CUTOFFS_16[5], 0); // SEW
				drawOffsetPaddedUFace(img, getRect(++offsetIterator), wireCross, B, CUTOFFS_16[16], 0, 0, 0, 0); // NSEW
			}
			else if(descriptor[1] == "BITANCHOR")
			{
				RGBAImage& baseTile = blockTextures[descriptor[2] + ".png"];
				drawAnchoredFace(img, getRect(offsetIterator), baseTile, B, false, false, false, false, true);  // U face
				drawAnchoredFace(img, getRect(++offsetIterator), baseTile, B, false, true, false, false, false);  // S face
				drawAnchoredFace(img, getRect(++offsetIterator), baseTile, B, false, false, true, false, false);  // W face
				drawAnchoredFace(img, getRect(++offsetIterator), baseTile, B, false, true, true, false, false);  // SW face
				drawAnchoredFace(img, getRect(++offsetIterator), baseTile, B, true, false, false, false, false);  // N face
				drawAnchoredFace(img, getRect(++offsetIterator), baseTile, B, true, true, false, false, false);  // NS face
				drawAnchoredFace(img, getRect(++offsetIterator), baseTile, B, true, false, true, false, false);  // NW face
				drawAnchoredFace(img, getRect(++offsetIterator), baseTile, B, true, true, true, false, false);  // NSW face
				drawAnchoredFace(img, getRect(++offsetIterator), baseTile, B, false, false, false, true, false);  // E face
				drawAnchoredFace(img, getRect(++offsetIterator), baseTile, B, false, true, false, true, false);  // SE face
				drawAnchoredFace(img, getRect(++offsetIterator), baseTile, B, false, false, true, true, false);  // WE face
				drawAnchoredFace(img, getRect(++offsetIterator), baseTile, B, false, true, true, true, false);  // SWE face
				drawAnchoredFace(img, getRect(++offsetIterator), baseTile, B, true, false, false, true, false);  // NE face
				drawAnchoredFace(img, getRect(++offsetIterator), baseTile, B, true, true, false, true, false);  // NSE face
				drawAnchoredFace(img, getRect(++offsetIterator), baseTile, B, true, false, true, true, false);  // NWE face
				drawAnchoredFace(img, getRect(++offsetIterator), baseTile, B, true, true, true, true, false);  // NSWE face
			}
			else if(descriptor[1] == "STEM")
			{
				RGBAImage stemTile;
				for(int i = 0; i < 8; i++, offsetIterator++)
				{
					stemTile = blockTextures[descriptor[2] + ".png_" + tostring(i)];
					drawItemBlockImage(img, getRect(offsetIterator), stemTile, B); // draw stem level 0-7
				}
				stemTile = blockTextures.at(descriptor[3] + ".png_0");
				RGBAImage& stemBent = blockTextures.at(descriptor[3] + ".png_1");
				drawPartialItemBlockImage(img, getRect(offsetIterator), stemTile, 0, false, true, true, false, false, B); // N
				drawPartialItemBlockImage(img, getRect(++offsetIterator), stemBent, 0, false, true, true, false, false, B); // S
				drawPartialItemBlockImage(img, getRect(++offsetIterator), stemTile, 0, false, false, false, true, true, B); // W
				drawPartialItemBlockImage(img, getRect(++offsetIterator), stemBent, 0, false, false, false, true, true, B); // E
			}
			else if(descriptor[1] == "REPEATER")
			{
				RGBAImage& baseTile = blockTextures.at(descriptor[2] + ".png");
				RGBAImage& torchTile = blockTextures.at(descriptor[3] + ".png");
				drawRepeater(img, getRect(offsetIterator), baseTile, torchTile, 1, B);  // repeater on N
				drawRepeater(img, getRect(++offsetIterator), baseTile, torchTile, 0, B);  // repeater on E
				drawRepeater(img, getRect(++offsetIterator), baseTile, torchTile, 3, B);  // repeater on S
				drawRepeater(img, getRect(++offsetIterator), baseTile, torchTile, 2, B);  // repeater on W
			}
			else if(descriptor[1] == "LEVER")
			{
				RGBAImage& baseTile = blockTextures.at(descriptor[2] + ".png");
				RGBAImage& leverTile = blockTextures.at(descriptor[3] + ".png");
				drawWallLever(img, getRect(offsetIterator), baseTile, leverTile, 1, B);  // wall lever facing E
				drawWallLever(img, getRect(++offsetIterator), baseTile, leverTile, 0, B);  // wall lever facing W
				drawWallLever(img, getRect(++offsetIterator), baseTile, leverTile, 3, B);  // wall lever facing S
				drawWallLever(img, getRect(++offsetIterator), baseTile, leverTile, 2, B);  // wall lever facing N
				drawFloorLeverNS(img, getRect(++offsetIterator), baseTile, leverTile, B);  // ground lever NS
				drawFloorLeverEW(img, getRect(++offsetIterator), baseTile, leverTile, B);  // ground lever WE
				drawCeilLever(img, getRect(++offsetIterator), leverTile, B);  // ground lever NS / WE
//# 34 LEVER (specify lever base texture, lever handle texture; like lever)
/*
				blockOffsets[offsetIdx(blockid, 1)] = blockOffsets[offsetIdx(blockid, 9)] = offsetIterator; // facing E
				blockOffsets[offsetIdx(blockid, 2)] = blockOffsets[offsetIdx(blockid, 10)] = ++offsetIterator; // facing W
				blockOffsets[offsetIdx(blockid, 3)] = blockOffsets[offsetIdx(blockid, 11)] = ++offsetIterator; // facing S
				blockOffsets[offsetIdx(blockid, 4)] = blockOffsets[offsetIdx(blockid, 12)] = ++offsetIterator; // facing N
				blockOffsets[offsetIdx(blockid, 5)] = blockOffsets[offsetIdx(blockid, 13)] = ++offsetIterator; // ground NS
				blockOffsets[offsetIdx(blockid, 6)] = blockOffsets[offsetIdx(blockid, 14)] = ++offsetIterator; // ground WE
				blockOffsets[offsetIdx(blockid, 0)] = blockOffsets[offsetIdx(blockid, 7)] = ++offsetIterator; // ceil NS
				blockOffsets[offsetIdx(blockid, 8)] = blockOffsets[offsetIdx(blockid, 15)] = offsetIterator; // ceil WE
				*/
			}
			else if(descriptor[1] == "SIGNPOST")
			{
				drawSign(img, getRect(offsetIterator), blockTextures.at((descriptor[2] + ".png")), blockTextures.at((descriptor[3] + ".png")), B);  // generic sign post
			}
			else if(blockid == 8) // water
			{
				if(blockTextures.count((descriptor[2] + ".png")) == 0)
				{
					offsetIterator += 11;
					continue;
				}
					
				RGBAImage& waterTile = blockTextures.at((descriptor[2] + ".png"));
				drawBlockImage(img, getRect(offsetIterator), waterTile, waterTile, waterTile, B);  // full water
				drawRotatedBlockImage(img, getRect(++offsetIterator), SourceTile(), SourceTile(), blockTile(waterTile), B);  // water surface
				drawRotatedBlockImage(img, getRect(++offsetIterator), SourceTile(), blockTile(waterTile), blockTile(waterTile), B);  // water missing W
				drawRotatedBlockImage(img, getRect(++offsetIterator), blockTile(waterTile), SourceTile(), blockTile(waterTile), B);  // water missing S
				
				drawPartialBlockImage(img, getRect(++offsetIterator), waterTile, waterTile, waterTile, B, CUTOFFS_16[2], 0, 0, 0, true);  // water level 7
				drawPartialBlockImage(img, getRect(++offsetIterator), waterTile, waterTile, waterTile, B, CUTOFFS_16[4], 0, 0, 0, true);  // water level 6
				drawPartialBlockImage(img, getRect(++offsetIterator), waterTile, waterTile, waterTile, B, CUTOFFS_16[6], 0, 0, 0, true);  // water level 5
				drawPartialBlockImage(img, getRect(++offsetIterator), waterTile, waterTile, waterTile, B, CUTOFFS_16[8], 0, 0, 0, true);  // water level 4
				drawPartialBlockImage(img, getRect(++offsetIterator), waterTile, waterTile, waterTile, B, CUTOFFS_16[10], 0, 0, 0, true);  // water level 3
				drawPartialBlockImage(img, getRect(++offsetIterator), waterTile, waterTile, waterTile, B, CUTOFFS_16[12], 0, 0, 0, true);  // water level 2
				drawPartialBlockImage(img, getRect(++offsetIterator), waterTile, waterTile, waterTile, B, CUTOFFS_16[14], 0, 0, 0, true);  // water level 1
					
			}
			else if(blockid == 10) // lava
			{
				if(blockTextures.count((descriptor[2] + ".png")) == 0)
				{
					offsetIterator += 4;
					continue;
				}
					
				RGBAImage& lavaTile = blockTextures.at((descriptor[2] + ".png"));
				drawBlockImage(img, getRect(offsetIterator), lavaTile, lavaTile, lavaTile, B);  // full lava
				drawPartialBlockImage(img, getRect(++offsetIterator), lavaTile, lavaTile, lavaTile, B, CUTOFFS_16[4], 0, 0, 0, true);  // lava level 3
				drawPartialBlockImage(img, getRect(++offsetIterator), lavaTile, lavaTile, lavaTile, B, CUTOFFS_16[8], 0, 0, 0, true);  // lava level 2
				drawPartialBlockImage(img, getRect(++offsetIterator), lavaTile, lavaTile, lavaTile, B, CUTOFFS_16[12], 0, 0, 0, true);  // lava level 1
			}
			else if(blockid == 26) // bed
			{
				RGBAImage& footFront = blockTextures.at((descriptor[2] + ".png"));
				RGBAImage& footSide = blockTextures.at((descriptor[3] + ".png"));
				RGBAImage& footTop = blockTextures.at((descriptor[4] + ".png"));
				RGBAImage& headFront = blockTextures.at((descriptor[5] + ".png"));
				RGBAImage& headSide = blockTextures.at((descriptor[6] + ".png"));
				RGBAImage& headTop = blockTextures.at((descriptor[7] + ".png"));
				drawPartialBlockImage(img, getRect(offsetIterator), footSide, footFront, footTop, B, true, false, true, CUTOFFS_16[8], 0, 0, 0, false); // bed foot pointing S
				drawPartialBlockImage(img, getRect(++offsetIterator), footFront, footSide, footTop, B, false, true, true, CUTOFFS_16[8], 0, 3, 2, false);  // bed foot pointing W
				drawPartialBlockImage(img, getRect(++offsetIterator), footSide, footFront, footTop, B, true, true, true, CUTOFFS_16[8], 0, 2, 1, false);  // bed foot pointing N
				drawPartialBlockImage(img, getRect(++offsetIterator), footFront, footSide, footTop, B, true, true, true, CUTOFFS_16[8], 0, 1, 0, false); // bed foot pointing E
				drawPartialBlockImage(img, getRect(++offsetIterator), headSide, headFront, headTop, B, true, true, true, CUTOFFS_16[8], 0, 0, 0, false); // bed head pointing S
				drawPartialBlockImage(img, getRect(++offsetIterator), headFront, headSide, headTop, B, true, true, true, CUTOFFS_16[8], 0, 3, 2, false);  // bed head pointing W
				drawPartialBlockImage(img, getRect(++offsetIterator), headSide, headFront, headTop, B, true, false, true, CUTOFFS_16[8], 0, 2, 1, false);  // bed head pointing N
				drawPartialBlockImage(img, getRect(++offsetIterator), headFront, headSide, headTop, B, false, true, true, CUTOFFS_16[8], 0, 1, 0, false); // bed head pointing E
			}
			else if(blockid == 117) // brewing stand
			{
				drawBrewingStand(img, getRect(offsetIterator), blockTextures.at((descriptor[2] + ".png")), blockTextures.at((descriptor[3] + ".png")), B);  // brewing stand
			}
			else if(blockid == 118) // cauldron
			{
				RGBAImage& cauldronSide = blockTextures.at((descriptor[2] + ".png"));
				RGBAImage& waterTile = blockTextures.at((descriptor[3] + ".png"));
				drawCauldron(img, getRect(offsetIterator), cauldronSide, waterTile, 0, B);  // cauldron empty
				drawCauldron(img, getRect(++offsetIterator), cauldronSide, waterTile, CUTOFFS_16[10], B);  // cauldron 1/3 full
				drawCauldron(img, getRect(++offsetIterator), cauldronSide, waterTile, CUTOFFS_16[6], B);  // cauldron 2/3 full
				drawCauldron(img, getRect(++offsetIterator), cauldronSide, waterTile, CUTOFFS_16[2], B);  // cauldron full
			}
			else if(blockid == 122) // dragon egg
			{
				drawDragonEgg(img, getRect(offsetIterator), blockTextures.at((descriptor[2] + ".png")), B);  // dragon egg
			}
			else if(blockid == 138) // beacon
			{
				int coverid;
				if(!fromstring(descriptor[3], coverid))
					coverid = 20; // glass
				drawBeacon(img, getRect(offsetIterator), blockTextures.at((descriptor[3] + ".png")),  blockTextures.at((descriptor[2] + ".png")), getRect(blockOffsets[offsetIdx(coverid, 0)]), B);
			}
			else if(blockid == 140) // flower pot
			{
				RGBAImage& flowerpotTile = blockTextures.at((descriptor[2] + ".png"));
				RGBAImage& fillerTile = blockTextures.at((descriptor[3] + ".png"));
				int contenttype = 0;
				drawFlowerPot(img, getRect(offsetIterator), flowerpotTile, fillerTile, false, fillerTile, contenttype, B); // flower pot [empty]
				for(int i = 4; i < descriptorsize; i++)
				{
					if(i == 12)
						contenttype = 1;
					else
						contenttype = 0;
					drawFlowerPot(img, getRect(++offsetIterator), flowerpotTile, fillerTile, true, blockTextures.at((descriptor[i] + ".png")), contenttype, B);
				}
			}
			else if(blockid == 145) // anvil
			{
				RGBAImage& basetexture = blockTextures.at((descriptor[2] + ".png"));
				RGBAImage& anvildamage0 = blockTextures.at((descriptor[3] + ".png"));
				RGBAImage& anvildamage1 = blockTextures.at((descriptor[4] + ".png"));
				RGBAImage& anvildamage2 = blockTextures.at((descriptor[5] + ".png"));
				drawAnvil(img, getRect(offsetIterator), basetexture, anvildamage0, 0, B);
				drawAnvil(img, getRect(++offsetIterator), basetexture, anvildamage0, 1, B);
				drawAnvil(img, getRect(++offsetIterator), basetexture, anvildamage1, 0, B);
				drawAnvil(img, getRect(++offsetIterator), basetexture, anvildamage1, 1, B);
				drawAnvil(img, getRect(++offsetIterator), basetexture, anvildamage2, 0, B);
				drawAnvil(img, getRect(++offsetIterator), basetexture, anvildamage2, 1, B);
			}
			else if(blockid == 154)
			{
				drawHopper(img, getRect(offsetIterator), blockTextures.at((descriptor[2] + ".png")), blockTextures.at((descriptor[3] + ".png")), B);  // hopper
			}
			offsetIterator++;
		}
	}

	return true;
}
