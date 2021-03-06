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


#include "ui_mainwindow.h"
#include "mainwindow.hpp"

#include "ProgressWatcher.hpp"
#include "viewer/imageCache.h"

#include "FullscreenViewer.hpp"

#include "color.hpp"
#include "debug.hpp"
#include "renders/AnimRender.hpp"
#include "Deteleciner.hpp"
#include "comparators/GradientComparator.hpp"
#include "containers/DelegatedContainer.hpp"
#include "containers/FrameContainer.hpp"
#include "containers/ImageContainer.hpp"
#include "containers/ImageContainerSaver.hpp"
#include "utils/Animator.hpp"
#include "utils/AProcessWatcher.hpp"
#include "utils/ImageLoader.hpp"

#include "savers/DumpSaver.hpp"
#include "visualisations/MovementGraph.hpp"
#include "Spinbox2D.hpp"

#include <vector>
#include <utility>

#include <QDesktopServices>
#include <QFileInfo>
#include <QFile>
#include <QMessageBox>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QInputDialog>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QFileDialog>
#include <QTime>
#include <QtConcurrent>
#include <QSettings>

using namespace Overmix;


void foldableGroupBox( QWidget* widget, bool enabled, QGroupBox* box ){
	auto update = [=]( bool checked )
		{ box->setMaximumHeight( checked ? QWIDGETSIZE_MAX : 20 ); };
	//TODO: find proper size
	
	widget->connect( box, &QGroupBox::clicked, update );
	box->setCheckable( true );
	box->setChecked( enabled );
	update( enabled );
}

main_widget::main_widget( ImageContainer& images )
	:	QMainWindow()
	,	ui(new Ui_main_widget)
#ifdef PORTABLE //Portable settings
	,	settings( QCoreApplication::applicationDirPath() + "/settings.ini", QSettings::IniFormat )
#else
	,	settings( "spillerrec", "overmix" )
#endif
	,	viewer( settings, (QWidget*)this )
	,	browser( settings, (QWidget*)this )
	,	images( images )
	,	aligner_config( this, true )
	,	 render_config( this, true )
	,	img_model( images )
{
	ui->setupUi(this);
	aligner_config.initialize();
	 render_config.initialize();
	ui->align_layout ->insertWidget( 0, &aligner_config );
	ui->render_layout->insertWidget( 0, & render_config );
	
	//Add 2D spinboxes
	scale_spinbox      = new DoubleSpinbox2D( this );
	deconvolve_spinbox = new DoubleSpinbox2D( this );
	blur_spinbox       = new       Spinbox2D( this );
	
	ui->scale_layout->addWidget( scale_spinbox );
	ui->post_deconvolve_layout->insertRow( 0, "Deviation", deconvolve_spinbox );
	ui->post_blur_layout->addWidget( blur_spinbox );
	
	scale_spinbox->setValue( {1.0, 1.0} );
	deconvolve_spinbox->setValue( {0.0, 0.0} );
	deconvolve_spinbox->setScale( 1.0 );
	
		 scale_spinbox->call( &QDoubleSpinBox::setSingleStep, 0.001 );
	deconvolve_spinbox->call( &QDoubleSpinBox::setSingleStep, 0.001 );
		 scale_spinbox->call( &QDoubleSpinBox::setDecimals, 3 );
	deconvolve_spinbox->call( &QDoubleSpinBox::setDecimals, 3 );
		 scale_spinbox->call( &QDoubleSpinBox::setRange, 0.001,  32.0 );
	deconvolve_spinbox->call( &QDoubleSpinBox::setRange, 0.000, 128.0 );
	
	
	//Buttons
	connect( ui->btn_clear,      SIGNAL( clicked() ), this, SLOT( clear_image()          ) );
	connect( ui->btn_refresh,    SIGNAL( clicked() ), this, SLOT( refresh_image()        ) );
	connect( ui->btn_save,       SIGNAL( clicked() ), this, SLOT( save_image()           ) );
	connect( ui->btn_save_files, SIGNAL( clicked() ), this, SLOT( save_files()           ) );
	connect( ui->btn_subpixel,   SIGNAL( clicked() ), this, SLOT( alignImage() ) );
	connect( ui->pre_alpha_mask, SIGNAL( clicked() ), this, SLOT( set_alpha_mask()       ) );
	connect( ui->pre_clear_mask, SIGNAL( clicked() ), this, SLOT( clear_mask()           ) );
	connect( ui->btn_as_mask,    SIGNAL( clicked() ), this, SLOT( use_current_as_mask()  ) );
	connect( ui->btn_apply_mods, SIGNAL( clicked() ), this, SLOT( applyModifications()  ) );
	update_draw();
	
	//Checkboxes
	connect( ui->cbx_interlaced, SIGNAL( toggled(bool) ), this, SLOT( change_interlace() ) );
	change_interlace();
	
	//Comparing changes
	connect( ui->cbx_merge_h,    SIGNAL( toggled(bool)     ), this, SLOT( updateComparator() ) );
	connect( ui->cbx_merge_v,    SIGNAL( toggled(bool)     ), this, SLOT( updateComparator() ) );
	connect( ui->merge_movement, SIGNAL( valueChanged(int) ), this, SLOT( updateComparator() ) );
	
	//Groupboxes
	foldableGroupBox( this, false, ui->preprocess_group  );
	foldableGroupBox( this, false, ui->comparing_group   );
	foldableGroupBox( this, true,  ui->merge_group       );
	foldableGroupBox( this, false, ui->render_group      );
	foldableGroupBox( this, false, ui->postprocess_group );
	foldableGroupBox( this, false, ui->color_group       );
	//foldableGroupBox( this, true,  ui->images_group      );
	foldableGroupBox( this, false, ui->masks_group       );
	foldableGroupBox( this, false, ui->selection_group   );
	
	//Reset aligner cache
	connect( &render_config, SIGNAL( changed() ), this, SLOT( resetImage() ) );
	connect( &img_model, SIGNAL( dataChanged(const QModelIndex&, const QModelIndex&) ), this, SLOT( resetImage() ) );
	
	//Menubar
	connect( ui->action_add_files,    SIGNAL( triggered() ), this, SLOT( open_image()     ) );
	connect( ui->action_save,         SIGNAL( triggered() ), this, SLOT( save_image()     ) );
	connect( ui->action_exit,         SIGNAL( triggered() ), this, SLOT( close()          ) );
	connect( ui->action_show_menubar, SIGNAL( triggered() ), this, SLOT( toggleMenubar()  ) );
	connect( ui->action_fullscreen,   SIGNAL( triggered() ), this, SLOT( showFullscreen() ) );
	connect( ui->action_online_wiki,  SIGNAL( triggered() ), this, SLOT( openOnlineHelp() ) );
	connect( ui->action_crop_all,     SIGNAL( triggered() ), this, SLOT( crop_all() ) );
	connect( ui->action_create_slide,     SIGNAL( triggered() ), this, SLOT( create_slide() ) );
	ui->action_show_menubar->setChecked( settings.value( "show_menubar", true ).toBool() );
	toggleMenubar();
	
	connect( ui->action_movement_graph, &QAction::triggered, [&](){ new MovementGraph( images ); } );
	
	
	//Add images
	//qRegisterMetaType<QList<QUrl> >( "QStringList" );
	connect( this, SIGNAL( urls_retrived(QStringList) ), this, SLOT( process_urls(QStringList) ), Qt::QueuedConnection );

	//Refresh info labels
	refresh_text();
	
	//Init files model
	ui->files_view->setModel( &img_model );
	ui->files_view->setColumnWidth( 0, 120 );
	connect( ui->files_view->selectionModel(), &QItemSelectionModel::selectionChanged
		,	this, &main_widget::browserChangeImage );
	connect( ui->btn_add_group,    SIGNAL(clicked()), this, SLOT(addGroup()) );
	connect( ui->btn_delete_files, SIGNAL(clicked()), this, SLOT(removeFiles()) );
	
	connect( ui->selection_selector, SIGNAL(activated(int)), this, SLOT(updateSelection()) );
	
	setAcceptDrops( true );
	ui->preview_layout->addWidget( &viewer );
	ui->files_layout  ->addWidget( &browser );
	viewer .setSizePolicy( QSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding ) );
	browser.setSizePolicy( QSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding ) );
	connect( &viewer, SIGNAL(double_clicked()), this, SLOT(showFullscreen()) );
	
	save_dir = settings.value( "save_directory", "." ).toString();
	if( settings.value( "remember_position", true ).toBool() )
		restoreGeometry( settings.value( "window_position" ).toByteArray() );
}

main_widget::~main_widget(){
	//TODO: fix imageViewer, so it cleans itself up!
	browser.change_image( nullptr );
	viewer.change_image( nullptr );
	delete ui;
}


void main_widget::dragEnterEvent( QDragEnterEvent *event ){
	if( event->mimeData()->hasUrls() )
		event->acceptProposedAction();
}
void main_widget::dropEvent( QDropEvent *event ){
	if( event->mimeData()->hasUrls() ){
		event->setDropAction( Qt::CopyAction );
		event->accept();
		
		QStringList files;
		for( auto url : event->mimeData()->urls() )
			files << url.toLocalFile();
		emit urls_retrived( files );
	}
}
void main_widget::closeEvent( QCloseEvent *event ){
	settings.setValue( "window_position", saveGeometry() );
	QWidget::closeEvent( event );
}


void main_widget::process_urls( QStringList files ){
	ProgressWatcher watcher( this, tr("Loading images") );
	ImageLoader::loadImages( files, images, detelecine, alpha_mask, &watcher );
	
	clear_cache();
	refresh_text();
	update_draw();
	update();
	ui->files_view->reset();
}


void main_widget::refresh_text(){
	auto& aligner = getAlignedImages();
	auto size       = aligner.size().size;
	auto imageCount = aligner.count();
	
	if( imageCount == 0 )
		ui->lbl_info->setText( tr( "No images" ) );
	else
		ui->lbl_info->setText(
				tr( "Size: " )
			+	QString::number( size.width() ) + "x"
			+	QString::number( size.height()) + " ("
			+	tr( "%n image(s) in ", nullptr, imageCount )
			+	tr( "%n frame(s))",    nullptr, aligner.getFrames().size() )
		);
}

static color_type color_from_spinbox( QSpinBox* spinbox ){
	return color::fromDouble( spinbox->value() / (double)spinbox->maximum() );
}

static ScalingFunction translateScaling( int id ){
	switch( id ){
		case 0:  return ScalingFunction::SCALE_NEAREST;
		case 1:  return ScalingFunction::SCALE_LINEAR;
		case 2:  return ScalingFunction::SCALE_CATROM;
		case 3:  return ScalingFunction::SCALE_MITCHELL;
		case 4:  return ScalingFunction::SCALE_SPLINE;
		case 5:  return ScalingFunction::SCALE_LANCZOS_3;
		case 6:  return ScalingFunction::SCALE_LANCZOS_5;
		case 7:  return ScalingFunction::SCALE_LANCZOS_7;
		default: return ScalingFunction::SCALE_NEAREST;
	}
}

const ImageEx& main_widget::postProcess( const ImageEx& input, bool new_image ){
	pipe_scaling.setWidth(  scale_spinbox->getValue().x );
	pipe_scaling.setHeight( scale_spinbox->getValue().y );
	pipe_scaling.setScaling( translateScaling( ui->post_scaling->currentIndex() ) );
	
	
	pipe_deconvolve.setDeviation( deconvolve_spinbox->getValue() );
	pipe_deconvolve.setIterations( ui->sbx_iterations->value() );
	
	pipe_blurring.setMethod( ui->cbx_blur->currentIndex() );
	pipe_blurring.setWidth( blur_spinbox->getValue().x );
	pipe_blurring.setHeight( blur_spinbox->getValue().x );
	
	pipe_edge.setMethod( ui->cbx_edge_filter->currentIndex() );
	
	pipe_level.setLimitMin( color_from_spinbox( ui->sbx_limit_min ) );
	pipe_level.setLimitMax( color_from_spinbox( ui->sbx_limit_max ) );
	pipe_level.setOutMin( color_from_spinbox( ui->sbx_out_min ) );
	pipe_level.setOutMax( color_from_spinbox( ui->sbx_out_max ) );
	pipe_level.setGamma( ui->dsbx_gamma->value() );
	
	//TODO: Sharpen
//	if( ui->cbx_sharpen->isChecked() )
//		edge->substract( *(*temp_ex)[0] );
	
	pipe_threshold.setThreshold( color_from_spinbox( ui->threshold_threshold ) );
	pipe_threshold.setSize( ui->threshold_size->value() );
	pipe_threshold.setMethod( ui->threshold_method->currentIndex() );
	
	return pipe_threshold.get( 
			pipe_level.get( 
			pipe_edge.get( 
			pipe_blurring.get( 
			pipe_deconvolve.get( 
			pipe_scaling.get( 
				{ input, new_image } ) ) ) ) ) ).first
		;
}


std::unique_ptr<ARender> main_widget::getRender() const
	{ return render_config.getRender(); }

ImageEx main_widget::renderImage( const AContainer& container ){
	ProgressWatcher watcher( this, "Rendering" );
	
	return getRender()->render( container, &watcher );
}

QImage main_widget::qrenderImage( const ImageEx& img ){
	if( !img.is_valid() )
		return QImage();
	
	auto image = postProcess( img, true );
	if( ui->cbx_nocolor->isChecked() )
		image = image.flatten();
	
	//Set settings
	unsigned setting = ImageEx::SETTING_NONE;
	if( ui->cbx_dither->isChecked() )
		setting = setting | ImageEx::SETTING_DITHER;
	if( ui->cbx_gamma->isChecked() )
		setting = setting | ImageEx::SETTING_GAMMA;
	
	//Render image
	//TODO: fix postProcess
	return image.to_qimage( setting );
}

void main_widget::refreshQImageCache(){
	if( renders.size() == 0 )
		return;
	
	//Render images and calculate offsets first
	//TODO: this is because the offset requires pipe_scaling to be updated, which is done by qrenderImage(). Improve?
	for( auto& render : renders ){
		render.qimg = qrenderImage( render.raw );
		render.qoffset = render.offset * pipe_scaling.getSize();
	}
	
	//Calculate full size, based on the qimage
	auto min_point = renders[0].qoffset;
	auto max_point = min_point;
	for( auto& render : renders ){
		min_point = min_point.min( render.qoffset );
		max_point = max_point.max( render.qoffset + Point<int>( render.qimg.size() ) );
	}
	auto full_size = max_point - min_point;
	
	//Expand the qimage with transparent areas, in order to have the same size, keeping them aligned with the offset
	for( auto& render : renders ){
		QImage current( full_size.width(), full_size.height(), QImage::Format_ARGB32 );
		current.fill( qRgba( 0,0,0,0 ) );
		
		QPainter painter( &current );
		painter.drawImage( render.qoffset.x, render.qoffset.y, render.qimg );
		
		render.qimg = current;
	}
}

imageCache* main_widget::createViewerCache() const{
	//TODO: proper frame timings
	auto cache = new imageCache();
	cache->set_info( renders.size(), renders.size() > 1, -1 );
	for( auto& render : renders )
		cache->add_frame( render.qimg, 1000*3/25 ); //3 frame animation delay, with 25 frames a second
	
	cache->set_fully_loaded();
	return cache;
}


void main_widget::refresh_image(){
	if( !images.isAligned() )
		alignImage();
	
	Timer t( "refresh_image" );
	auto start = getAlignedImages().minPoint();
	if( renders.size() == 0 ){
		auto frames = getAlignedImages().getFrames();
		renders.reserve( frames.size() );
		
		auto render = getRender();
		
		if( frames.size() == 1 )
			renders.emplace_back( renderImage( getAlignedImages() ), Point<double>(0.0,0.0) );
		else{
			//TODO: watcher
			AnimRender anim( getAlignedImages(), *render );
			for( auto& frame : frames ){
				FrameContainer current( getAlignedImages(), frame ); //TODO: remove requirement of this!
				auto img = anim.render( frame );
				if( img.is_valid() )
					renders.emplace_back( std::move(img), current.minPoint()-start );
			}
		}
	}
	
	refreshQImageCache();
	
	ui->btn_as_mask->setEnabled( renders.size() == 1 && renders[0].raw.getColorSpace().isGray() );
	viewer.change_image( createViewerCache(), true );
	refresh_text();
	ui->btn_save->setEnabled( true );
}

QString main_widget::getSavePath( QString title, QString file_types ){
	auto filename = QFileDialog::getSaveFileName( this, title, save_dir, file_types );
	
	//Remember the folder we saved in
	if( !filename.isEmpty() ){
		save_dir = QFileInfo( filename ).dir().absolutePath();
		settings.setValue( "save_directory", save_dir );
	}
	
	return filename;
}

void main_widget::save_image(){
	for( auto& render : renders ){
		QString filename = getSavePath( tr("Save image"), tr("PNG files (*.png);; dump files (*.dump)") );
		if( !filename.isEmpty() ){
			if( QFileInfo( filename ).suffix() == "dump" )
				DumpSaver( postProcess( render.raw, true ), filename ).exec(); //TODO: fix postProcess
			else
				render.qimg.save( filename );
		}
	}
}

void main_widget::save_files(){
	auto filename = getSavePath( tr("Save alignment"), tr("XML (*.xml.overmix)") );
	if( !filename.isEmpty() ){
		auto error = ImageContainerSaver::save( images, filename );
		if( !error.isEmpty() )
			QMessageBox::warning( this, tr("Could not save alignment"), error );
	}
}

void main_widget::clear_cache(){
	//TODO: a lot of this should probably be in resetImage!
	ui->btn_save->setEnabled( false );
	resetImage();
	for( auto& render : renders )
		render.qimg = QImage();
	viewer.change_image( nullptr );
}

void main_widget::clear_image(){
	clear_cache();
	clear_mask();
	detelecine.clear();
	
	pipe_scaling.invalidate();
	pipe_deconvolve.invalidate();
	pipe_blurring.invalidate();
	pipe_edge.invalidate();
	pipe_level.invalidate();
	pipe_threshold.invalidate();
	
	images.clear();
	browser.change_image( nullptr );
	ui->files_view->reset();
	
	selection = nullptr;
	ui->selection_selector->setCurrentIndex( 0 );
	
	refresh_text();
	update_draw();
}

void main_widget::alignImage(){
	Timer t( "alignImage" );
	clear_cache(); //Prevent any animation from running
	ProgressWatcher watcher( this, "Aligning" );
	
	auto aligner = aligner_config.getAligner();
	if( ui->cbx_each_frame->isChecked() ){
		//TODO: support displaying several frames in watcher
		for( auto frame : getAlignedImages().getFrames() ){
			FrameContainer container( getAlignedImages(), frame );
			aligner->align( container, &watcher );
		}
	}
	else
		aligner->align( getAlignedImages(), &watcher );
	
	clear_cache();
	refresh_text();
	update_draw();
}

void main_widget::change_interlace(){
	detelecine.setEnabled( ui->cbx_interlaced->isChecked() );
}

void main_widget::set_alpha_mask(){
	QString filename = QFileDialog::getOpenFileName( this, tr("Open alpha mask"), save_dir, tr("PNG files (*.png)") );
	
	if( !filename.isEmpty() ){
		alpha_mask = images.addMask( std::move( ImageEx::fromFile( filename )[0] ) );
		ui->pre_clear_mask->setEnabled( true );
	}
}

void main_widget::clear_mask(){
	alpha_mask = -1;
	ui->pre_clear_mask->setEnabled( false );
}

void main_widget::use_current_as_mask(){
	if( renders.size() == 1 ){
		//TODO: postProcess cache no longer valid as we have several frames
		alpha_mask = images.addMask( Plane( postProcess(renders[0].raw, false)[0] ) );
		auto& aligner = getAlignedImages();
		for( unsigned i=0; i<aligner.count(); i++ )
			aligner.setMask( i, alpha_mask );
	}
}

void main_widget::update_draw(){
	ui->btn_refresh->setText( images.isAligned() ? tr( "Draw" ) : tr( "Align&&Draw" ) );
	ui->btn_refresh->setEnabled( images.count() > 0 );
}


void main_widget::addGroup(){
	auto name = QInputDialog::getText( this, tr("New group"), tr("Enter group name") );
	if( !name.isEmpty() ){
		const auto& indexes = ui->files_view->selectionModel()->selectedIndexes();
		if( indexes.size() > 0 )
			img_model.addGroup( name, indexes.front(), indexes.back() );
		else
			images.addGroup( name );
		ui->files_view->reset();
	}
}

static QImage fromSelection( const ImagesModel& model, const QModelIndexList& indexes ){
	if( indexes.size() > 0 )
		return model.getImage( indexes.front() );
	return QImage();
}

void main_widget::browserChangeImage( const QItemSelection& selected, const QItemSelection& ){
	auto img = fromSelection( img_model, selected.indexes() );
	if( !img.isNull() )
		browser.change_image( new imageCache( img ), true );
}

void main_widget::removeFiles(){
	auto indexes = ui->files_view->selectionModel()->selectedRows();
	if( indexes.size() > 0 )
		img_model.removeRows( indexes.front().row(), indexes.size(), img_model.parent(indexes.front()) );
	refresh_text();
	resetImage();
}

void main_widget::showFullscreen(){
	if( ui->tab_pages->currentIndex() != 0 ){
		auto img = fromSelection( img_model, ui->files_view->selectionModel()->selectedIndexes() );
		if( !img.isNull() )
			FullscreenViewer::show( settings, img, this );
	}
	else if( renders.size() > 0 )
		FullscreenViewer::show( settings, createViewerCache(), this );
}


void main_widget::open_image(){
	auto formats = QImageReader::supportedImageFormats();
	auto filter = QString( "*.dump" );
	for( auto f : formats )
		filter += " *." + f;
	
	process_urls( QFileDialog::getOpenFileNames( this, "Select images", "", "Images (" + filter + ")" ) );
}

void main_widget::toggleMenubar(){
	if( ui->action_show_menubar->isChecked() )
		ui->menuBar->show();
	else
		ui->menuBar->hide();
}

void main_widget::openOnlineHelp(){
	QDesktopServices::openUrl( QUrl( "https://github.com/spillerrec/Overmix/wiki" ) );
}

AContainer& main_widget::getAlignedImages(){
	return selection ? *selection : images;
}

void main_widget::applyModifications(){
	//Crop
	auto left   = ui->crop_left  ->value();
	auto top    = ui->crop_top   ->value();
	auto right  = ui->crop_right ->value();
	auto bottom = ui->crop_bottom->value();
	
	//Deconvolve
	double   deviation      = ui->pre_deconvolve_deviation ->value();
	Point<double> deviation_both( deviation, deviation ); //TODO: use spinbox
	unsigned dev_iterations = ui->pre_deconvolve_iterations->value();
	
	//Scale
	auto scale_method = translateScaling( ui->pre_scale_method->currentIndex() );
	Point<double> scale( ui->pre_scale_width->value(), ui->pre_scale_height->value() );
	
	auto& container = getAlignedImages();
	ProgressWatcher( this, "Applying modifications" ).loopAll( container.count(), [&]( int i ){
			if( deviation > 0.0009 && dev_iterations > 0 )
				container.imageRef( i ).apply( &Plane::deconvolve_rl, deviation_both, dev_iterations );
			
			container.cropImage( i, left, top, right, bottom );
			container.scaleImage( i, scale, scale_method );
			
			if( ui->convert_rgb->isChecked() )
				container.imageRef(i) = container.imageRef(i).toRgb();
			else if( ui->convert_devlc->isChecked() )
				container.imageRef(i) = deVlcImage( container.imageRef(i) );
		} );
	
	clear_cache();
}

void main_widget::updateSelection(){
	auto getIndex = [&]( QString title, QString label, int max_value, auto on_accept ){
			bool ok = false;
			auto result = QInputDialog::getInt( this, title, label, 1, 1, max_value, 1, &ok );
			if( ok )
				on_accept( result - 1 );
		};
	
	switch( ui->selection_selector->currentIndex() ){
		case 1:
				getIndex( tr( "Select group" ), tr( "Select the group number" ), images.groupAmount()
					, [&]( int index ){
						selection = std::make_unique<DelegatedContainer>( images.getGroup( index ) );
					} );
			break;
		case 2: { //Select frame
				auto frames = images.getFrames();
				getIndex( tr( "Select frame" ), tr( "Select the frame number" ), frames.size()
					, [&]( int index ){
						selection = std::make_unique<FrameContainer>( images, frames[index] );
					} );
			} break;
			
		case 3: QMessageBox::warning( this, tr("Not implemented"), tr("Custom selection not yet implemented") );
			//TODO: implement this obviously
		case 0:
		default: selection = nullptr; break;
	}
	
	clear_cache();
	refresh_text();
}

void main_widget::updateComparator(){
	GradientComparator g;
	
	auto ver = ui->cbx_merge_v->isChecked();
	auto hor = ui->cbx_merge_h->isChecked();
	if( hor && ver )
		g.method = AlignMethod::BOTH;
	else
		g.method = ver ? AlignMethod::VER : AlignMethod::HOR;
	
	g.movement = ui->merge_movement->value() / 100.0;
	
	images.setComparator( g );
}

void main_widget::crop_all(){
	debug::output_rectable( images,
		{{  QInputDialog::getInt( this, tr("Pick area"), tr("x") )
		 ,  QInputDialog::getInt( this, tr("Pick area"), tr("y") )
		 }
		,{  QInputDialog::getInt( this, tr("Pick area"), tr("width") )
		 ,  QInputDialog::getInt( this, tr("Pick area"), tr("height") )
		 }
	});
	
	clear_cache();
}

void main_widget::create_slide(){
	Animator anim;
	anim.render( renders[0].raw );
}
