/*
	This file is part of Overmix.

	Overmix is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	Overmix is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Overmix.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "image.h"

#include <QRect>
#include <vector>
#include <QtConcurrentMap>

#include <png.h>

using namespace std;

image::image( unsigned w, unsigned h ){
	height = h;
	width = w;
	data = new color[ h * w ];
}
image::image( QImage img ){
	height = img.height();
	width = img.width();
	data = new color[ height * width ];
	
	for( unsigned iy=0; iy<height; iy++ ){
		const QRgb *row = (const QRgb*)img.constScanLine( iy );
		color* row2 = scan_line( iy );
		for( unsigned ix=0; ix<width; ++ix, ++row, ++row2 )
			*row2 = color( *row );
	}
}
image::image( const char* path ){
	if( !from_png( path ) )
		from_dump( path );
}

image::~image(){
	qDebug( "deleting image %p", this );
	if( data )
		delete[] data;
}


bool image::from_dump( const char* path ){
	FILE *f = fopen( path, "rb" );
	if( !f )
		return false;
	
	fread( &width, sizeof(unsigned), 1, f );
	fread( &height, sizeof(unsigned), 1, f );
	qDebug( "read: %dx%d", width, height );
	unsigned depth;
	fread( &depth, sizeof(unsigned), 1, f );
	
	data = new color[ height * width ];
	
	for( unsigned iy=0; iy<height; iy++ ){
		color* row = scan_line( iy );
		for( unsigned ix=0; ix<width; ++ix, ++row ){
			unsigned short c=127*255;
			fread( &c, 2, 1, f );
			c <<= 6;
			*row = color( c,c,c );
		}
	}
	
	unsigned sub_width, sub_height;
	fread( &sub_width, sizeof(unsigned), 1, f );
	fread( &sub_height, sizeof(unsigned), 1, f );
	qDebug( "read: %dx%d", width, height );
	unsigned sub_depth;
	fread( &sub_depth, sizeof(unsigned), 1, f );
	
	for( unsigned iy=0; iy<sub_height; iy++ ){
		color* row1 = scan_line( iy*2 );
		color* row2 = scan_line( iy*2+1 );
		for( unsigned ix=0; ix<sub_width; ++ix ){
			unsigned short c=127*255;
			fread( &c, 2, 1, f );
			c <<= 6;
			row1[ix*2].g = c;
			row1[ix*2+1].g = c;
			row2[ix*2].g = c;
			row2[ix*2+1].g = c;
		}
	}
	fread( &sub_width, sizeof(unsigned), 1, f );
	fread( &sub_height, sizeof(unsigned), 1, f );
	qDebug( "read: %dx%d", width, height );
	fread( &sub_depth, sizeof(unsigned), 1, f );
	
	for( unsigned iy=0; iy<sub_height; iy++ ){
		color* row1 = scan_line( iy*2 );
		color* row2 = scan_line( iy*2+1 );
		for( unsigned ix=0; ix<sub_width; ++ix ){
			unsigned short c=127*255;
			fread( &c, 2, 1, f );
			c <<= 6;
			row1[ix*2].b = c;
			row1[ix*2+1].b = c;
			row2[ix*2].b = c;
			row2[ix*2+1].b = c;
		}
	}
	
	fclose( f );
	return true;
}


static int read_chunk_callback_thing( png_structp ptr, png_unknown_chunkp chunk ){
	return 0;
}

bool image::from_png( const char* path ){
	FILE *f = fopen( path, "rb" );
	if( !f )
		return false;
	
	//Read signature
	unsigned char header[8];
	fread( &header, 1, 8, f );
	
	//Check signature
	if( png_sig_cmp( header, 0, 8 ) ){
		fclose( f );
		return false;
	}
	
	//Start initializing libpng
	png_structp png_ptr = png_create_read_struct( PNG_LIBPNG_VER_STRING, NULL, NULL, NULL );
	if( !png_ptr ){
		png_destroy_read_struct( &png_ptr, NULL, NULL );
		fclose( f );
		return false;
	}
	
	png_infop info_ptr = png_create_info_struct( png_ptr );
	if( !info_ptr ){
		png_destroy_read_struct( &png_ptr, NULL, NULL );
		fclose( f );
		return false;
	}
	
	png_infop end_info = png_create_info_struct( png_ptr );
	if( !end_info ){
		png_destroy_read_struct( &png_ptr, &info_ptr, NULL );
		fclose( f );
		return false;
	}
	
	png_init_io( png_ptr, f );
	png_set_sig_bytes( png_ptr, 8 );
	png_set_read_user_chunk_fn( png_ptr, NULL, read_chunk_callback_thing );
	
	
	//Finally start reading
	png_read_png( png_ptr, info_ptr, PNG_TRANSFORM_STRIP_16 | PNG_TRANSFORM_EXPAND, NULL );
	png_bytep *row_pointers = png_get_rows( png_ptr, info_ptr );
	
	height = png_get_image_height( png_ptr, info_ptr );
	width = png_get_image_width( png_ptr, info_ptr );
	data = new color[ height * width ];
	
	for( unsigned iy=0; iy<height; iy++ ){
		color* row = scan_line( iy );
		for( unsigned ix=0; ix<width; ix++ ){
			int r = row_pointers[iy][ix*3 + 0] * 256;
			int g = row_pointers[iy][ix*3 + 1] * 256;
			int b = row_pointers[iy][ix*3 + 2] * 256;
			row[ix] = color( r, g, b );
		}
	}
	
	return true;
}


double image::diff( const image& img, int x, int y ) const{
	unsigned long long difference = 0;
	unsigned amount = 0;
	
	QRect r1( QPoint(0,0), QSize( width, height ) );
	QRect r2( QPoint(x,y), QSize( img.get_width(), img.get_height() ) );
	QRect common = r1.intersected( r2 );
	
	for( int iy=common.y(); iy<common.height()+common.y(); iy++ ){
		const color* row1 = scan_line( iy );
		const color* row2 = img.scan_line( iy-y ) - x;
		
		for( int ix=common.x(); ix<common.width()+common.x(); ix++ ){
			color p1 = row1[ix];
			color p2 = row2[ix];
			if( p1.a > 127*256 && p2.a > 127*256 ){
				/* color d = p1.difference( p2 );
				difference += d.r;
				difference += d.g;
				difference += d.b; */
				
				difference += p2.r > p1.r ? p2.r-p1.r : p1.r-p2.r;
				amount++;
			}
		}
	}
	//qDebug( "%d %d - %d %d %d %d - %d",x,y, common.x(), common.y(), common.width(), common.height(), difference  / amount );
	
	return amount ? ((double)difference / (double)amount / 256.0) : 999999;
}


struct img_comp{
	image* img1;
	image* img2;
	int h_middle;
	int v_middle;
	double diff;
	int level;
	int left;
	int right;
	int top;
	int bottom;
	
	img_comp( image& image1, image& image2, int hm, int vm, int lvl=0, int l=0, int r=0, int t=0, int b=0 ){
		img1 = &image1;
		img2 = &image2;
		h_middle = hm;
		v_middle = vm;
		diff = -1;
		level = lvl;
		left = l;
		right = r;
		top = t;
		bottom = b;
	}
	void do_diff( int x, int y ){
		if( diff < 0 )
			diff = img1->diff( *img2, x, y );
	}
	
	MergeResult result() const{
		if( level > 0 )
			return img1->best_round_sub( *img2, level, left, right, h_middle, top, bottom, v_middle, diff );
		else
			return MergeResult(QPoint( h_middle, v_middle ),diff);
	}
};
void do_diff_center( img_comp& comp ){
	comp.do_diff( comp.h_middle, comp.v_middle );
}

MergeResult image::best_round( image& img, int level, double range_x, double range_y ){
	//Bail if invalid settings
	if(	level < 1
		||	( range_x < 0.0 || range_x > 1.0 )
		||	( range_y < 0.0 || range_y > 1.0 )
		||	is_invalid()
		||	img.is_invalid()
		)
		return MergeResult(QPoint(),99999);
	
	//Starting point is the one where both images are centered on each other
	int x = ( (int)width - img.get_width() ) / 2;
	int y = ( (int)height - img.get_height() ) / 2;
	
	return best_round_sub(
			img, level
		,	((int)1 - (int)img.get_width()) * range_x, ((int)width - 1) * range_x, x
		,	((int)1 - (int)img.get_height()) * range_y, ((int)height - 1) * range_y, y
		,	diff( img, x,y )
		);
}

MergeResult image::best_round_sub( image& img, int level, int left, int right, int h_middle, int top, int bottom, int v_middle, double diff ){
//	qDebug( "Round %d: %d,%d,%d x %d,%d,%d at %.2f", level, left, h_middle, right, top, v_middle, bottom, diff );
	QList<img_comp> comps;
	int amount = level*2 + 2;
	double h_offset = (double)(right - left) / amount;
	double v_offset = (double)(bottom - top) / amount;
	level = level > 1 ? level-1 : 1;
	
	if( h_offset < 1 && v_offset < 1 ){
		//Handle trivial step
		//Check every diff in the remaining area
		for( int ix=left; ix<=right; ix++ )
			for( int iy=top; iy<=bottom; iy++ ){
				img_comp t( *this, img, ix, iy );
				if( ix == h_middle && iy == v_middle )
					t.diff = diff;
				comps << t;
			}
	}
	else{
		//Make sure we will not do the same position multiple times
		double h_add = ( h_offset < 1 ) ? 1 : h_offset;
		double v_add = ( v_offset < 1 ) ? 1 : v_offset;
		
		for( double iy=top+v_offset; iy<=bottom; iy+=v_add )
			for( double ix=left+h_offset; ix<=right; ix+=h_add ){
				int x = ( ix < 0.0 ) ? ceil( ix-0.5 ) : floor( ix+0.5 );
				int y = ( iy < 0.0 ) ? ceil( iy-0.5 ) : floor( iy+0.5 );
				
				//Avoid right-most case. Can't be done in the loop
				//as we always want it to run at least once.
				if( ( x == right && x != left ) || ( y == bottom && y != top ) )
					continue;
				
				//Create and add
				img_comp t(
						*this, img, x, y, level
					,	floor( ix - h_offset ), ceil( ix + h_offset )
					,	floor( iy - v_offset ), ceil( iy + v_offset )
					);
				
				if( x == h_middle && y == v_middle )
					t.diff = diff; //Reuse old diff
				
				comps << t;
			}
	}
	
	//Calculate diffs
	QtConcurrent::map( comps, do_diff_center ).waitForFinished();
	
	//Find best comp
	const img_comp* best = NULL;
	double best_diff = 99999;
	
	for( int i=0; i<comps.size(); i++ ){
		if( comps.at(i).diff < best_diff ){
			best = &comps.at(i);
			best_diff = best->diff;
		}
	}
	
	if( !best ){
		qDebug( "ERROR! no result to continue on!!" );
		return MergeResult(QPoint(),99999);
	}
	
	return best->result();
}

