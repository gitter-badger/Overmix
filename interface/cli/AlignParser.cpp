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


#include "Parsers.hpp"
#include "Parsing.hpp"

#include "aligners/AnimationSeparator.hpp"
#include "aligners/AverageAligner.hpp"
#include "aligners/FakeAligner.hpp"
#include "aligners/FrameAligner.hpp"
#include "aligners/FrameCalculatorAligner.hpp"
#include "aligners/LinearAligner.hpp"
#include "aligners/RecursiveAligner.hpp"
#include "aligners/SuperResAligner.hpp"
#include "planes/ImageEx.hpp"

#include <QString>

using namespace std;
using namespace Overmix;

static void convert( QString str, AlignMethod& func ){
	func = getEnum<AlignMethod>( str,
		{	{ "both", AlignMethod::BOTH }
		,	{ "ver",  AlignMethod::VER  }
		,	{ "hor",  AlignMethod::HOR  }
		} );
}

static void convert( QString str, AlignSettings& settings ){
	Splitter split( str, ';' );
	convert( split.left,  settings.method );
	convert( split.right, settings.movement );
}

static unique_ptr<AAligner> makeAligner( QString name, QString parameters ){
	if( name == "AnimationSeparator" )
		return convertUnique<AnimationSeparator,AlignSettings,double,bool>( parameters );
	else if( name == "Average" )
		return convertUnique<AverageAligner,AlignSettings,double>( parameters );
	else if( name == "Fake" )
		return make_unique<FakeAligner>();
	else if( name == "Frame" )
		return convertUnique<FrameAligner, AlignMethod>( parameters );
	else if( name == "FrameCalculator" )
		return convertUnique<FrameCalculatorAligner, int, int, int>( parameters );
	else if( name == "Linear" )
		return convertUnique<LinearAligner, AlignMethod>( parameters );
	else if( name == "Recursive" )
		return convertUnique<RecursiveAligner,AlignSettings,double>( parameters );
	else if( name == "SuperRes" )
		return convertUnique<SuperResAligner,AlignMethod,double>( parameters );
	else
		throw std::invalid_argument( fromQString( "No aligner found with the name: '" + name + "'" ) );
}

void Overmix::alignerParser( QString parameters, AContainer& container ){
	Splitter split( parameters, ':' );
	makeAligner( split.left, split.right )->align( container );
}
