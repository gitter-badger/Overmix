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

#ifndef IMAGE_EX_HPP
#define IMAGE_EX_HPP

#include "Plane.hpp"
#include <QPoint>
#include <QImage>

class image;

struct PlaneInfo{
	public:
		Plane &p;
		double offset_x;
		double offset_y;
		unsigned spacing_x;
		unsigned spacing_y;
		
		PlaneInfo( Plane &p, double off_x, double off_y, unsigned spacing ) : p( p ){
			offset_x = off_x;
			offset_y = off_y;
			spacing_x = spacing_y = spacing;
		}
};

class ImageEx{
	public:
		enum system{
			RGB,
			YUV
		};
		enum YuvSystem{
			SYSTEM_KEEP,
			SYSTEM_REC601,
			SYSTEM_REC709
		};
		enum Settings{
			SETTING_NONE = 0x0,
			SETTING_DITHER = 0x1,
			SETTING_GAMMA = 0x2
		};
	
	private:
		bool initialized;
		Plane **planes;
		PlaneInfo **infos;
		system type;
		bool initialize_size( unsigned size );
		bool read_dump_plane( FILE *f, unsigned index );
		bool from_dump( const char* path );
		bool from_png( const char* path );
		
	public:
		ImageEx( system type = RGB ) : type( type ){
			initialized = false;
			planes = new Plane*[4];
			infos = new PlaneInfo*[4];
			for( int i=0; i<4; i++ ){
				planes[i] = 0;
				infos[i] = 0;
			}
		}
		~ImageEx(){
			for( int i=0; i<4; i++ ){
				if( planes[i] )
					delete planes[i];
				if( infos[i] )
					delete infos[i];
			}
			
			if( planes )
				delete[] planes;
			if( infos )
				delete[] infos;
		}
		
		bool create( unsigned width, unsigned height );
		
		bool is_valid() const{ return initialized; }
		
		bool read_file( const char* path );
		Plane* alpha_plane() const{ return planes[3]; }
		
		QImage to_qimage( YuvSystem system, unsigned setting=SETTING_NONE );
		
		unsigned get_width() const{
			return (*this)[0].p.get_width();
		}
		unsigned get_height() const{
			return (*this)[0].p.get_height();
		}
		
		system get_system() const{ return type; }
		
		
		double diff( const ImageEx& img, int x, int y ) const;
		bool is_interlaced() const;
		void replace_line( ImageEx& img, bool top );
		void combine_line( ImageEx& img, bool top );
		
		MergeResult best_vertical( const ImageEx& img, int level, double range, DiffCache *cache=nullptr ) const{
			return best_round( img, level, 0, range, cache );
		}
		MergeResult best_horizontal( const ImageEx& img, int level, double range, DiffCache *cache=nullptr ) const{
			return best_round( img, level, range, 0, cache );
		}
		MergeResult best_round( const ImageEx& img, int level, double range_x, double range_y, DiffCache *cache=nullptr ) const;
		
		PlaneInfo& operator[]( const unsigned index ) const{
			return *infos[index];
		}
};

#endif