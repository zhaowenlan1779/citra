﻿// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <memory>
#include <utility>
#include <QMessageBox>
#include <QTimer>
#include "citra_qt/configuration/config.h"
#include "citra_qt/configuration/configure_input.h"
#include "common/param_package.h"

const std::array<std::string, ConfigureInput::ANALOG_SUB_BUTTONS_NUM>
    ConfigureInput::analog_sub_buttons{{
        "up",
        "down",
        "left",
        "right",
        "modifier",
    }};

static QString getKeyName(int key_code) {
    switch (key_code) {
    case Qt::Key_Shift:
        return QObject::tr("Shift");
    case Qt::Key_Control:
        return QObject::tr("Ctrl");
    case Qt::Key_Alt:
        return QObject::tr("Alt");
    case Qt::Key_Meta:
        return "";
    default:
        return QKeySequence(key_code).toString();
    }
}

static void SetAnalogButton(const Common::ParamPackage& input_param,
                            Common::ParamPackage& analog_param, const std::string& button_name) {
    if (analog_param.Get("engine", "") != "analog_from_button") {
        analog_param = {
            {"engine", "analog_from_button"},
            {"modifier_scale", "0.5"},
        };
    }
    analog_param.Set(button_name, input_param.Serialize());
}

static QString ButtonToText(const Common::ParamPackage& param) {
    if (!param.Has("engine")) {
        return QObject::tr("[not set]");
    } else if (param.Get("engine", "") == "keyboard") {
        return getKeyName(param.Get("code", 0));
    } else if (param.Get("engine", "") == "sdl") {
        QString text = QString(QObject::tr("Joystick %1")).arg(param.Get("joystick", "").c_str());
        if (param.Has("hat")) {
            text += QString(QObject::tr(" Hat %1 %2"))
                        .arg(param.Get("hat", "").c_str(), param.Get("direction", "").c_str());
        }
        if (param.Has("axis")) {
            text += QString(QObject::tr(" Axis %1%2"))
                        .arg(param.Get("axis", "").c_str(), param.Get("direction", "").c_str());
        }
        if (param.Has("button")) {
            text += QString(QObject::tr(" Button %1")).arg(param.Get("button", "").c_str());
        }
        return text;
    } else {
        return QObject::tr("[unknown]");
    }
};

static QString AnalogToText(const Common::ParamPackage& param, const std::string& dir) {
    if (!param.Has("engine")) {
        return QObject::tr("[not set]");
    } else if (param.Get("engine", "") == "analog_from_button") {
        return ButtonToText(Common::ParamPackage{param.Get(dir, "")});
    } else if (param.Get("engine", "") == "sdl") {
        if (dir == "modifier") {
            return QString(QObject::tr("[unused]"));
        }

        QString text = QString(QObject::tr("Joystick %1")).arg(param.Get("joystick", "").c_str());
        if (dir == "left" || dir == "right") {
            text += QString(QObject::tr(" Axis %1")).arg(param.Get("axis_x", "").c_str());
        } else if (dir == "up" || dir == "down") {
            text += QString(QObject::tr(" Axis %1")).arg(param.Get("axis_y", "").c_str());
        }
        return text;
    } else {
        return QObject::tr("[unknown]");
    }
};

ConfigureInput::ConfigureInput(QWidget* parent)
    : QWidget(parent), ui(std::make_unique<Ui::ConfigureInput>()),
      timeout_timer(std::make_unique<QTimer>()), poll_timer(std::make_unique<QTimer>()) {

    ui->setupUi(this);
    setFocusPolicy(Qt::ClickFocus);

    button_map = {
        ui->buttonA,        ui->buttonB,        ui->buttonX,         ui->buttonY,  ui->buttonDpadUp,
        ui->buttonDpadDown, ui->buttonDpadLeft, ui->buttonDpadRight, ui->buttonL,  ui->buttonR,
        ui->buttonStart,    ui->buttonSelect,   ui->buttonZL,        ui->buttonZR, ui->buttonHome,
    };

    analog_map_buttons = {{
        {
            ui->buttonCircleUp,
            ui->buttonCircleDown,
            ui->buttonCircleLeft,
            ui->buttonCircleRight,
            ui->buttonCircleMod,
        },
        {
            ui->buttonCStickUp,
            ui->buttonCStickDown,
            ui->buttonCStickLeft,
            ui->buttonCStickRight,
            nullptr,
        },
    }};

    analog_map_stick = {ui->buttonCircleAnalog, ui->buttonCStickAnalog};

    for (int button_id = 0; button_id < Settings::NativeButton::NumButtons; button_id++) {
        if (button_map[button_id])
            connect(button_map[button_id], &QPushButton::released, [=]() {
                handleClick(
                    button_map[button_id],
                    [=](const Common::ParamPackage& params) { buttons_param[button_id] = params; },
                    InputCommon::Polling::DeviceType::Button);
            });
    }

    for (int analog_id = 0; analog_id < Settings::NativeAnalog::NumAnalogs; analog_id++) {
        for (int sub_button_id = 0; sub_button_id < ANALOG_SUB_BUTTONS_NUM; sub_button_id++) {
            if (analog_map_buttons[analog_id][sub_button_id] != nullptr) {
                connect(analog_map_buttons[analog_id][sub_button_id], &QPushButton::released,
                        [=]() {
                            handleClick(analog_map_buttons[analog_id][sub_button_id],
                                        [=](const Common::ParamPackage& params) {
                                            SetAnalogButton(params, analogs_param[analog_id],
                                                            analog_sub_buttons[sub_button_id]);
                                        },
                                        InputCommon::Polling::DeviceType::Button);
                        });
            }
        }
        connect(analog_map_stick[analog_id], &QPushButton::released, [=]() {
            QMessageBox::information(
                this, "Information",
                "After pressing OK, first move your joystick horizontally, and then vertically.");
            handleClick(
                analog_map_stick[analog_id],
                [=](const Common::ParamPackage& params) { analogs_param[analog_id] = params; },
                InputCommon::Polling::DeviceType::Analog);
        });
    }

    connect(ui->buttonRestoreDefaults, &QPushButton::released, [this]() { restoreDefaults(); });

    timeout_timer->setSingleShot(true);
    connect(timeout_timer.get(), &QTimer::timeout, [this]() { setPollingResult({}, true); });

    connect(poll_timer.get(), &QTimer::timeout, [this]() {
        Common::ParamPackage params;
        for (auto& poller : device_pollers) {
            params = poller->GetNextInput();
            if (params.Has("engine")) {
                setPollingResult(params, false);
                return;
            }
        }
    });

    this->loadConfiguration();

    // TODO(wwylele): enable this when we actually emulate it
    ui->buttonHome->setEnabled(false);
}

void ConfigureInput::applyConfiguration() {
    std::transform(buttons_param.begin(), buttons_param.end(), Settings::values.buttons.begin(),
                   [](const Common::ParamPackage& param) { return param.Serialize(); });
    std::transform(analogs_param.begin(), analogs_param.end(), Settings::values.analogs.begin(),
                   [](const Common::ParamPackage& param) { return param.Serialize(); });
}

void ConfigureInput::loadConfiguration() {
    std::transform(Settings::values.buttons.begin(), Settings::values.buttons.end(),
                   buttons_param.begin(),
                   [](const std::string& str) { return Common::ParamPackage(str); });
    std::transform(Settings::values.analogs.begin(), Settings::values.analogs.end(),
                   analogs_param.begin(),
                   [](const std::string& str) { return Common::ParamPackage(str); });
    updateButtonLabels();
}

void ConfigureInput::restoreDefaults() {
    for (int button_id = 0; button_id < Settings::NativeButton::NumButtons; button_id++) {
        buttons_param[button_id] = Common::ParamPackage{
            InputCommon::GenerateKeyboardParam(Config::default_buttons[button_id])};
    }

    for (int analog_id = 0; analog_id < Settings::NativeAnalog::NumAnalogs; analog_id++) {
        for (int sub_button_id = 0; sub_button_id < ANALOG_SUB_BUTTONS_NUM; sub_button_id++) {
            Common::ParamPackage params{InputCommon::GenerateKeyboardParam(
                Config::default_analogs[analog_id][sub_button_id])};
            SetAnalogButton(params, analogs_param[analog_id], analog_sub_buttons[sub_button_id]);
        }
    }
    updateButtonLabels();
    applyConfiguration();
}

void ConfigureInput::updateButtonLabels() {
    for (int button = 0; button < Settings::NativeButton::NumButtons; button++) {
        button_map[button]->setText(ButtonToText(buttons_param[button]));
    }

    for (int analog_id = 0; analog_id < Settings::NativeAnalog::NumAnalogs; analog_id++) {
        for (int sub_button_id = 0; sub_button_id < ANALOG_SUB_BUTTONS_NUM; sub_button_id++) {
            if (analog_map_buttons[analog_id][sub_button_id]) {
                analog_map_buttons[analog_id][sub_button_id]->setText(
                    AnalogToText(analogs_param[analog_id], analog_sub_buttons[sub_button_id]));
            }
        }
        analog_map_stick[analog_id]->setText(tr("Set Analog Stick"));
    }
}

void ConfigureInput::handleClick(QPushButton* button,
                                 std::function<void(const Common::ParamPackage&)> new_input_setter,
                                 InputCommon::Polling::DeviceType type) {
    button->setText(tr("[press key]"));
    button->setFocus();

    input_setter = new_input_setter;

    device_pollers = InputCommon::Polling::GetPollers(type);

    // Keyboard keys can only be used as button devices
    want_keyboard_keys = type == InputCommon::Polling::DeviceType::Button;

    for (auto& poller : device_pollers) {
        poller->Start();
    }

    grabKeyboard();
    grabMouse();
    timeout_timer->start(5000); // Cancel after 5 seconds
    poll_timer->start(200);     // Check for new inputs every 200ms
}

void ConfigureInput::setPollingResult(const Common::ParamPackage& params, bool abort) {
    releaseKeyboard();
    releaseMouse();
    timeout_timer->stop();
    poll_timer->stop();
    for (auto& poller : device_pollers) {
        poller->Stop();
    }

    if (!abort) {
        (*input_setter)(params);
    }

    updateButtonLabels();
    input_setter = boost::none;
}

void ConfigureInput::keyPressEvent(QKeyEvent* event) {
    if (!input_setter || !event)
        return;

    if (event->key() != Qt::Key_Escape) {
        if (want_keyboard_keys) {
            setPollingResult(Common::ParamPackage{InputCommon::GenerateKeyboardParam(event->key())},
                             false);
        } else {
            // Escape key wasn't pressed and we don't want any keyboard keys, so don't stop polling
            return;
        }
    }
    setPollingResult({}, true);
}

void ConfigureInput::retranslateUi() {
    ui->retranslateUi(this);
}
