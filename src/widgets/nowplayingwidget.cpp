/* This file is part of Clementine.
   Copyright 2010, David Sansome <me@davidsansome.com>

   Clementine is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Clementine is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Clementine.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "fullscreenhypnotoad.h"
#include "nowplayingwidget.h"
#include "core/application.h"
#include "covers/albumcoverloader.h"
#include "covers/coverproviders.h"
#include "covers/currentartloader.h"
#include "covers/kittenloader.h"
#include "library/librarybackend.h"
#include "ui/albumcoverchoicecontroller.h"
#include "ui/iconloader.h"

#include <QMenu>
#include <QMovie>
#include <QPainter>
#include <QPaintEvent>
#include <QSettings>
#include <QSignalMapper>
#include <QTextDocument>
#include <QTimeLine>
#include <QtDebug>

const char* NowPlayingWidget::kSettingsGroup = "NowPlayingWidget";

const char* NowPlayingWidget::kHypnotoadPath = ":/hypnotoad.gif";

// Space between the cover and the details in small mode
const int NowPlayingWidget::kPadding = 2;

// Width of the transparent to black gradient above and below the text in large
// mode
const int NowPlayingWidget::kGradientHead = 40;
const int NowPlayingWidget::kGradientTail = 20;

// Maximum height of the cover in large mode, and offset between the
// bottom of the cover and bottom of the widget
const int NowPlayingWidget::kMaxCoverSize = 260;
const int NowPlayingWidget::kBottomOffset = 0;

// Border for large mode
const int NowPlayingWidget::kTopBorder = 4;


NowPlayingWidget::NowPlayingWidget(QWidget* parent)
  : QWidget(parent),
    app_(NULL),
    album_cover_choice_controller_(new AlbumCoverChoiceController(this)),
    mode_(SmallSongDetails),
    menu_(new QMenu(this)),
    above_statusbar_action_(NULL),
    visible_(false),
    small_ideal_height_(0),
    show_hide_animation_(new QTimeLine(500, this)),
    fade_animation_(new QTimeLine(1000, this)),
    details_(new QTextDocument(this)),
    previous_track_opacity_(0.0),
    bask_in_his_glory_action_(NULL),
    aww_(false),
    kittens_(NULL),
    pending_kitten_(0)
{
  // Load settings
  QSettings s;
  s.beginGroup(kSettingsGroup);
  mode_ = Mode(s.value("mode", SmallSongDetails).toInt());

  // Accept drops for setting album art
  setAcceptDrops(true);

  // Context menu
  QActionGroup* mode_group = new QActionGroup(this);
  QSignalMapper* mode_mapper = new QSignalMapper(this);
  connect(mode_mapper, SIGNAL(mapped(int)), SLOT(SetMode(int)));
  CreateModeAction(SmallSongDetails, tr("Small album cover"), mode_group, mode_mapper);
  CreateModeAction(LargeSongDetails, tr("Large album cover"), mode_group, mode_mapper);

  menu_->addActions(mode_group->actions());
  menu_->addSeparator();

  QList<QAction*> actions = album_cover_choice_controller_->GetAllActions();

  connect(album_cover_choice_controller_->cover_from_file_action(),
          SIGNAL(triggered()), this, SLOT(LoadCoverFromFile()));
  connect(album_cover_choice_controller_->cover_to_file_action(),
          SIGNAL(triggered()), this, SLOT(SaveCoverToFile()));
  connect(album_cover_choice_controller_->cover_from_url_action(),
          SIGNAL(triggered()), this, SLOT(LoadCoverFromURL()));
  connect(album_cover_choice_controller_->search_for_cover_action(),
          SIGNAL(triggered()), this, SLOT(SearchForCover()));
  connect(album_cover_choice_controller_->unset_cover_action(),
          SIGNAL(triggered()), this, SLOT(UnsetCover()));
  connect(album_cover_choice_controller_->show_cover_action(),
          SIGNAL(triggered()), this, SLOT(ShowCover()));

  menu_->addActions(actions);
  menu_->addSeparator();
  above_statusbar_action_ = menu_->addAction(tr("Show above status bar"));

  above_statusbar_action_->setCheckable(true);
  connect(above_statusbar_action_, SIGNAL(toggled(bool)), SLOT(ShowAboveStatusBar(bool)));
  above_statusbar_action_->setChecked(s.value("above_status_bar", false).toBool());

  bask_in_his_glory_action_ = menu_->addAction(tr("ALL GLORY TO THE HYPNOTOAD"));
  bask_in_his_glory_action_->setVisible(false);
  connect(bask_in_his_glory_action_, SIGNAL(triggered()), SLOT(Bask()));

  // Animations
  connect(show_hide_animation_, SIGNAL(frameChanged(int)), SLOT(SetHeight(int)));
  setMaximumHeight(0);

  connect(fade_animation_, SIGNAL(valueChanged(qreal)), SLOT(FadePreviousTrack(qreal)));
  fade_animation_->setDirection(QTimeLine::Backward); // 1.0 -> 0.0

  UpdateHeight();
}

NowPlayingWidget::~NowPlayingWidget() {
}

void NowPlayingWidget::SetApplication(Application* app) {
  app_ = app;

  album_cover_choice_controller_->SetApplication(app_);
  connect(app_->current_art_loader(), SIGNAL(ArtLoaded(Song,QString,QImage)),
          SLOT(AlbumArtLoaded(Song,QString,QImage)));
}

void NowPlayingWidget::CreateModeAction(Mode mode, const QString &text, QActionGroup *group, QSignalMapper* mapper) {
  QAction* action = new QAction(text, group);
  action->setCheckable(true);
  mapper->setMapping(action, mode);
  connect(action, SIGNAL(triggered()), mapper, SLOT(map()));

  if (mode == mode_)
    action->setChecked(true);
}

void NowPlayingWidget::set_ideal_height(int height) {
  small_ideal_height_ = height;
  UpdateHeight();
}

QSize NowPlayingWidget::sizeHint() const {
  return QSize(cover_loader_options_.desired_height_, total_height_);
}

void NowPlayingWidget::UpdateHeight() {
  switch (mode_) {
  case SmallSongDetails:
    cover_loader_options_.desired_height_ = small_ideal_height_;
    total_height_ = small_ideal_height_;
    break;

  case LargeSongDetails:
    cover_loader_options_.desired_height_ = qMin(kMaxCoverSize, width());
    total_height_ = kTopBorder + cover_loader_options_.desired_height_ + kBottomOffset;
    break;
  }

  // Update the animation settings and resize the widget now if we're visible
  show_hide_animation_->setFrameRange(0, total_height_);
  if (visible_ && show_hide_animation_->state() != QTimeLine::Running)
    setMaximumHeight(total_height_);

  // Re-scale the current image
  if (metadata_.is_valid()) {
    ScaleCover();
  }

  // Tell Qt we've changed size
  updateGeometry();
}

void NowPlayingWidget::Stopped() {
  SetVisible(false);
}

void NowPlayingWidget::UpdateDetailsText() {
  QString html;

  switch (mode_) {
    case SmallSongDetails:
      details_->setTextWidth(-1);
      details_->setDefaultStyleSheet("");
      html += "<p>";
      break;

    case LargeSongDetails:
      details_->setTextWidth(cover_loader_options_.desired_height_);
      details_->setDefaultStyleSheet("p {"
          "  font-size: small;"
          "  color: white;"
          "}");
      html += "<p align=center>";
      break;
  }

  // TODO: Make this configurable
  html += QString("<i>%1</i><br/>%2<br/>%3").arg(
      Qt::escape(metadata_.PrettyTitle()), Qt::escape(metadata_.artist()),
      Qt::escape(metadata_.album()));

  html += "</p>";
  details_->setHtml(html);
}

void NowPlayingWidget::ScaleCover() {
  cover_ = QPixmap::fromImage(
        AlbumCoverLoader::ScaleAndPad(cover_loader_options_, original_));
  update();
}

void NowPlayingWidget::KittenLoaded(quint64 id, const QImage& image) {
  if (aww_ && pending_kitten_ == id) {
    SetImage(image);
  }
}

void NowPlayingWidget::AlbumArtLoaded(const Song& metadata, const QString&,
                                      const QImage& image) {
  metadata_ = metadata;

  if (aww_) {
    pending_kitten_ = kittens_->LoadKitten(app_->current_art_loader()->options());
    return;
  }

  SetImage(image);
}

void NowPlayingWidget::SetImage(const QImage& image) {
  if (visible_) {
    // Cache the current pixmap so we can fade between them
    previous_track_ = QPixmap(size());
    previous_track_.fill(palette().background().color());
    previous_track_opacity_ = 1.0;
    QPainter p(&previous_track_);
    DrawContents(&p);
    p.end();
  }

  original_ = image;

  UpdateDetailsText();
  ScaleCover();
  SetVisible(true);

  // Were we waiting for this cover to load before we started fading?
  if (!previous_track_.isNull()) {
    fade_animation_->start();
  }
}

void NowPlayingWidget::SetHeight(int height) {
  setMaximumHeight(height);
}

void NowPlayingWidget::SetVisible(bool visible) {
  if (visible == visible_)
    return;
  visible_ = visible;

  show_hide_animation_->setDirection(visible ? QTimeLine::Forward : QTimeLine::Backward);
  show_hide_animation_->start();
}

void NowPlayingWidget::paintEvent(QPaintEvent *e) {
  QPainter p(this);

  DrawContents(&p);

  // Draw the previous track's image if we're fading
  if (!previous_track_.isNull()) {
    p.setOpacity(previous_track_opacity_);
    p.drawPixmap(0, 0, previous_track_);
  }
}

void NowPlayingWidget::DrawContents(QPainter *p) {
  switch (mode_) {
  case SmallSongDetails:
    if (hypnotoad_) {
      p->drawPixmap(0, 0, small_ideal_height_, small_ideal_height_, hypnotoad_->currentPixmap());
    } else {
      // Draw the cover
      p->drawPixmap(0, 0, small_ideal_height_, small_ideal_height_, cover_);
    }

    // Draw the details
    p->translate(small_ideal_height_ + kPadding, 0);
    details_->drawContents(p);
    p->translate(-small_ideal_height_ - kPadding, 0);
    break;

  case LargeSongDetails:
    const int total_size = qMin(kMaxCoverSize, width());
    const int x_offset = (width() - cover_loader_options_.desired_height_) / 2;

    // Draw the black background
    p->fillRect(QRect(0, kTopBorder, width(), height() - kTopBorder), Qt::black);

    // Draw the cover
    if (hypnotoad_) {
      p->drawPixmap(x_offset, kTopBorder, total_size, total_size, hypnotoad_->currentPixmap());
    } else {
      p->drawPixmap(x_offset, kTopBorder, total_size, total_size, cover_);
    }

    // Work out how high the text is going to be
    const int text_height = details_->size().height();
    const int gradient_mid = height() - qMax(text_height, kBottomOffset);

    // Draw the black fade
    QLinearGradient gradient(0, gradient_mid - kGradientHead,
                             0, gradient_mid + kGradientTail);
    gradient.setColorAt(0, QColor(0, 0, 0, 0));
    gradient.setColorAt(1, QColor(0, 0, 0, 255));

    p->fillRect(0, gradient_mid - kGradientHead,
                width(), height() - (gradient_mid - kGradientHead), gradient);

    // Draw the text on top
    p->translate(x_offset, height() - text_height);
    details_->drawContents(p);
    p->translate(-x_offset, -height() + text_height);
    break;
  }
}

void NowPlayingWidget::FadePreviousTrack(qreal value) {
  previous_track_opacity_ = value;
  if (qFuzzyCompare(previous_track_opacity_, qreal(0.0))) {
    previous_track_ = QPixmap();
  }

  update();
}

void NowPlayingWidget::SetMode(int mode) {
  mode_ = Mode(mode);
  UpdateHeight();
  UpdateDetailsText();
  update();

  QSettings s;
  s.beginGroup(kSettingsGroup);
  s.setValue("mode", mode_);
}

void NowPlayingWidget::resizeEvent(QResizeEvent* e) {
  if (visible_ && mode_ == LargeSongDetails && e->oldSize() != e->size()) {
    UpdateHeight();
    UpdateDetailsText();
  }
}

void NowPlayingWidget::contextMenuEvent(QContextMenuEvent* e) {
  // initial 'enabled' values depending on the kitty mode
  album_cover_choice_controller_->cover_from_file_action()->setEnabled(!aww_);
  album_cover_choice_controller_->cover_from_url_action()->setEnabled(!aww_);
  album_cover_choice_controller_->search_for_cover_action()->setEnabled(
        !aww_ && app_->cover_providers()->HasAnyProviders());
  album_cover_choice_controller_->unset_cover_action()->setEnabled(!aww_);
  album_cover_choice_controller_->show_cover_action()->setEnabled(!aww_);

  // some special cases
  if (!aww_) {
    const bool art_is_not_set = metadata_.has_manually_unset_cover()
        || (metadata_.art_automatic().isEmpty() && metadata_.art_manual().isEmpty());

    album_cover_choice_controller_->unset_cover_action()->setEnabled(!art_is_not_set);
    album_cover_choice_controller_->show_cover_action()->setEnabled(!art_is_not_set);
  }

  bask_in_his_glory_action_->setVisible(static_cast<bool>(hypnotoad_));

  // show the menu
  menu_->popup(mapToGlobal(e->pos()));
}

void NowPlayingWidget::ShowAboveStatusBar(bool above) {
  QSettings s;
  s.beginGroup(kSettingsGroup);
  s.setValue("above_status_bar", above);

  emit ShowAboveStatusBarChanged(above);
}

bool NowPlayingWidget::show_above_status_bar() const {
  return above_statusbar_action_->isChecked();
}

void NowPlayingWidget::AllHail(bool hypnotoad) {
  if (hypnotoad) {
    hypnotoad_.reset(new QMovie(kHypnotoadPath, QByteArray(), this));
    connect(hypnotoad_.get(), SIGNAL(updated(const QRect&)), SLOT(update()));
    hypnotoad_->start();
    update();
  } else {
    hypnotoad_.reset();
    update();
  }
}

void NowPlayingWidget::EnableKittens(bool aww) {
  if (!kittens_ && aww) {
    kittens_ = new KittenLoader(this);
    app_->MoveToNewThread(kittens_);
    connect(kittens_, SIGNAL(ImageLoaded(quint64,QImage)), SLOT(KittenLoaded(quint64,QImage)));
  }

  aww_ = aww;
}

void NowPlayingWidget::LoadCoverFromFile() {
  album_cover_choice_controller_->LoadCoverFromFile(&metadata_);
}

void NowPlayingWidget::LoadCoverFromURL() {
  album_cover_choice_controller_->LoadCoverFromURL(&metadata_);
}

void NowPlayingWidget::SearchForCover() {
  album_cover_choice_controller_->SearchForCover(&metadata_);
}

void NowPlayingWidget::SaveCoverToFile() {
  album_cover_choice_controller_->SaveCoverToFile(metadata_, original_);
}

void NowPlayingWidget::UnsetCover() {
  album_cover_choice_controller_->UnsetCover(&metadata_);
}

void NowPlayingWidget::ShowCover() {
  album_cover_choice_controller_->ShowCover(metadata_);
}

void NowPlayingWidget::Bask() {
  big_hypnotoad_.reset(new FullscreenHypnotoad);
  big_hypnotoad_->showFullScreen();
}

void NowPlayingWidget::dragEnterEvent(QDragEnterEvent* e) {
  if (AlbumCoverChoiceController::CanAcceptDrag(e)) {
    e->acceptProposedAction();
  }

  QWidget::dragEnterEvent(e);
}

void NowPlayingWidget::dropEvent(QDropEvent* e) {
  album_cover_choice_controller_->SaveCover(&metadata_, e);

  QWidget::dropEvent(e);
}
