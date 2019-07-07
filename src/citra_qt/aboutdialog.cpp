// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QIcon>
#include "aboutdialog.h"
#include "common/scm_rev.h"
#include "sd_import/decryptor.h"
#include "sd_import/disa_container.h"
#include "sd_import/inner_fat.h"
#include "ui_aboutdialog.h"

#include "common/logging/log.h"

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent, Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowSystemMenuHint),
      ui(new Ui::AboutDialog) {
    ui->setupUi(this);
    ui->labelLogo->setPixmap(QIcon::fromTheme("citra").pixmap(200));
    ui->labelBuildInfo->setText(
        ui->labelBuildInfo->text().arg(Common::g_build_fullname, Common::g_scm_branch,
                                       Common::g_scm_desc, QString(Common::g_build_date).left(10)));

    // Test place
    SDMCDecryptor importer(
        "J:/Nintendo 3DS/7a6f0c67f43bb9854033150131650864/005c012a47805bd85343333200035344");
    auto data = importer.DecryptFile("/title/00040000/00055e00/data/00000001.sav");
    DISAContainer container(data);
    auto content = container.GetIVFCLevel4Data();
    if (content.size() == 1) {
        LOG_INFO(Frontend, "content[0] size is {}", content[0].size());
        InnerFAT fat(content[0]);
        fat.WriteMetadata("H:/00000001.metadata");
    }
}

AboutDialog::~AboutDialog() = default;
