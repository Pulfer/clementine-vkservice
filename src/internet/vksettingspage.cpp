#include "vksettingspage.h"

#include <QDir>
#include <QFileDialog>

#include "ui_vksettingspage.h"
#include "core/application.h"
#include "core/logging.h"
#include "internet/vkservice.h"


VkSettingsPage::VkSettingsPage(SettingsDialog *parent)
    : SettingsPage(parent),
      ui_(new Ui::VkSettingsPage),
      service_(dialog()->app()->internet_model()->Service<VkService>())
{
    ui_->setupUi(this);
    connect(service_, SIGNAL(LoginSuccess(bool)),
            SLOT(LoginSuccess(bool)));
    connect(ui_->choose_path, SIGNAL(clicked()),
            SLOT(CasheDirBrowse()));
    connect(ui_->reset, SIGNAL(clicked()),
            SLOT(ResetCasheFilenames()));
}

VkSettingsPage::~VkSettingsPage()
{
    delete ui_;
}

void VkSettingsPage::Load()
{
    QSettings s;
    s.beginGroup(VkService::kSettingGroup);

    ui_->maxGlobalSearch->setValue(s.value("maxSearchResult",100).toInt());

    bool enable_cashing = s.value("enable_cashing", false).toBool();
    ui_->enable_cashing->setChecked(enable_cashing);
    QString def_path = QDir::toNativeSeparators(QDir::homePath() + "/Vk Cashe");
    ui_->cashe_dir->setText(s.value("cashe_path",def_path).toString());
    ui_->cashe_filename->setText(s.value("cashe_filename","%artist - %title").toString());

    if (service_->HasAccount()) {
        Login();
    } else {
        Logout();
    }
}

void VkSettingsPage::Save()
{
    QSettings s;
    s.beginGroup(VkService::kSettingGroup);

    s.setValue("maxSearchResult",ui_->maxGlobalSearch->value());
    s.setValue("enable_cashing",ui_->enable_cashing->isChecked());
    s.setValue("cashe_path",ui_->cashe_dir->text());
    s.setValue("cashe_filename", ui_->cashe_filename->text());
}

void VkSettingsPage::Login()
{
    qLog(Debug) << "Login clicked";
    ui_->account->setEnabled(false);
    service_->Login();
}

void VkSettingsPage::LoginSuccess(bool succ)
{
    qLog(Debug) << "LoginSuccess" << succ;
    if (succ) {
        ui_->login_button->setText("Logout");

        ui_->name->setText("Loading...");
        connect(service_, SIGNAL(NameUpdated(QString)),
                ui_->name, SLOT(setText(QString)));

        connect(ui_->login_button, SIGNAL(clicked()),
                SLOT(Logout()));
        disconnect(ui_->login_button, SIGNAL(clicked()),
                   this, SLOT(Login()));
    }
    ui_->account->setEnabled(true);
}

void VkSettingsPage::Logout()
{
    qLog(Debug) << "Logout clicked";
    service_->Logout();

    ui_->login_button->setText("Login");
    ui_->name->setText("");

    connect(ui_->login_button, SIGNAL(clicked()),
            SLOT(Login()));
    disconnect(ui_->login_button, SIGNAL(clicked()),
               this, SLOT(Logout()));
    ui_->account->setEnabled(true);
}

void VkSettingsPage::CasheDirBrowse()
{
    QString directory = QFileDialog::getExistingDirectory(
          this, tr("Choose Vk.com cashe directory"), ui_->cashe_dir->text());
    if (directory.isEmpty())
      return;

    ui_->cashe_dir->setText(QDir::toNativeSeparators(directory));
}

void VkSettingsPage::ResetCasheFilenames()
{
    ui_->cashe_filename->setText("%artist - %title");
}

