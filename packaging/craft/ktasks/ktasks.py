# SPDX-License-Identifier: GPL-3.0-or-later
# Craft blueprint used by the Android release job. The job copies it into the
# craft-blueprints-kde checkout and points srcDir at the repository.
import info
from Blueprints.CraftPackageObject import CraftPackageObject
from CraftCore import CraftCore


class subinfo(info.infoclass):
    def setTargets(self):
        self.svnTargets["master"] = "https://github.com/MarleyS439/ktasks.git"
        self.defaultTarget = "master"
        self.displayName = "KTasks"
        self.description = "Tasks, routines and journal, local or synchronized with Nextcloud"

    def setDependencies(self):
        self.buildDependencies["virtual/base"] = None
        self.buildDependencies["kde/frameworks/extra-cmake-modules"] = None
        self.runtimeDependencies["libs/qt/qtbase"] = None
        self.runtimeDependencies["libs/qt/qtdeclarative"] = None
        self.runtimeDependencies["libs/qt/qtsvg"] = None
        self.runtimeDependencies["libs/qt/qtmultimedia"] = None
        self.runtimeDependencies["libs/openssl"] = None
        self.runtimeDependencies["kde/frameworks/tier1/kirigami"] = None
        self.runtimeDependencies["kde/frameworks/tier1/ki18n"] = None
        self.runtimeDependencies["kde/frameworks/tier1/kcoreaddons"] = None
        self.runtimeDependencies["kde/frameworks/tier1/kconfig"] = None
        self.runtimeDependencies["kde/frameworks/tier1/kcalendarcore"] = None
        self.runtimeDependencies["kde/frameworks/tier2/kcolorscheme"] = None
        self.runtimeDependencies["kde/frameworks/tier3/knotifications"] = None
        self.runtimeDependencies["kde/unreleased/kirigami-addons"] = None
        self.runtimeDependencies["qt-libs/qtkeychain"] = None
        if CraftCore.compiler.platform.isAndroid:
            self.runtimeDependencies["kde/plasma/qqc2-breeze-style"] = None
        else:
            self.runtimeDependencies["kde/frameworks/tier1/breeze-icons"] = None
            self.runtimeDependencies["kde/frameworks/tier3/qqc2-desktop-style"] = None
            self.runtimeDependencies["kde/frameworks/tier2/kstatusnotifieritem"] = None
            self.runtimeDependencies["kde/frameworks/tier1/kwindowsystem"] = None
            self.runtimeDependencies["kde/frameworks/tier3/kiconthemes"] = None


class Package(CraftPackageObject.get("kde").pattern):
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.subinfo.options.configure.args += ["-DBUILD_TESTING=OFF"]
