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

#ifndef COMMAND_PARSER_HPP
#define COMMAND_PARSER_HPP

class QStringList;
namespace Overmix{
class ImageContainer;

class CommandParser{
	private:
		ImageContainer& images;
		bool use_gui{ true };
		
	public:
		CommandParser( ImageContainer& images ) : images(images) { }
		
		void parse( QStringList commands );
		bool useGui() const{ return use_gui; }
};

}

#endif