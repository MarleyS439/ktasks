(function () {
  const root = document.documentElement;
  const themeBtn = document.getElementById("themeToggle");
  const themeLabel = document.getElementById("themeLabel");
  const sunIcon = document.getElementById("themeIconSun");
  const moonIcon = document.getElementById("themeIconMoon");
  const langBtn = document.getElementById("langToggle");
  const langLabel = document.getElementById("langLabel");

  const previewImg = document.getElementById("previewImage");
  const windowTitle = document.getElementById("windowTitle");
  const zoomBtn = document.getElementById("screenshotZoomBtn");
  const lightbox = document.getElementById("lightbox");
  const lightboxImg = document.getElementById("lightboxImg");
  const lightboxClose = document.getElementById("lightboxClose");

  const strings = {
    pt: {
      panelBadge: "KDE Plasma 6",
      navFeatures: "Recursos",
      navArch: "Privacidade",
      navCompat: "Compatibilidade",
      navInstall: "Instalar",
      heroKicker: "Aplicativo Comunitário KDE · v1.0.2",
      heroTitle: "Organize tarefas, foco e rotina em sintonia com o KDE Plasma",
      heroLead: "Um aplicativo de produtividade construído com C++20, Qt 6, Kirigami e SQLite. Seus dados ficam seguros localmente e sincronizam de forma opcional com Nextcloud Tasks, CalDAV e Notes.",
      ctaDownload: "Baixar e Instalar",
      ctaRelease: "Release v1.0.2",
      tabSummary: "Resumo",
      tabMatrix: "Matriz",
      zoomHint: "Clique para ampliar",
      featEyebrow: "Ecossistema Completo",
      featTitle: "Tudo do seu fluxo diário em uma interface Kirigami limpa",
      featSub: "Projetado para parecer parte nativa do Plasma 6, respeitando tema, esquema de cores Breeze, fontes e ícones do seu desktop.",
      f1Title: "Painel de Resumo & Clima",
      f1Desc: "Saudação personalizada, dia da semana, KPIs clicáveis, previsão compacta Open-Meteo para 10 dias (com atualização horária), rotina do dia e notas recentes.",
      f2Title: "Matriz de Eisenhower & Triagem",
      f2Desc: "Classifique tarefas nos quadrantes Q1–Q4 com arrastar e soltar, rolagem fluida, promoção automática por prazo e modo Triagem Guiada sem distrações.",
      f3Title: "Pomodoro & Insights",
      f3Desc: "Barra persistente de foco e cronômetro por tarefa, histórico de execução, precisão das estimativas e gráficos de atividade para 1, 7 e 30 dias.",
      f4Title: "Listas, Subtarefas & Seleção em Lote",
      f4Desc: "Captura em linguagem natural (PT/EN), seleção múltipla estilo Dolphin com Ctrl e Shift, Desfazer/Refazer atômico, recorrência RRULE e exportação para PDF/.ics.",
      f5Title: "Rotina & Diário AES-256-GCM",
      f5Desc: "Blocos diários e semanais de rotina além de cadernos em Markdown com trava local por senha criptografada em AES-256-GCM antes de qualquer envio.",
      f6Title: "Nextcloud & Agenda Externa",
      f6Desc: "Sincronização incremental CalDAV (VTODO e VEVENT) + Nextcloud Notes API, com leitura passiva e separada de arquivos .ics / KOrganizer no Resumo.",
      archEyebrow: "Arquitetura & Segurança",
      archTitle: "Local primeiro, criptografado e sem telemetria",
      p1Title: "SQLite WAL + Backups Locais",
      p1Desc: "Seus dados vivem em ~/.local/share/ktasks/ktasks.db com snapshots automáticos pré-migração. Funciona 100% offline, sem conta obrigatória.",
      p2Title: "Credenciais no Cofre do Sistema",
      p2Desc: "Tokens Nextcloud (via Login Flow v2) e CalDAV ficam guardados no KWallet / Secret Service via QtKeychain — nunca em texto puro ou logs.",
      p3Title: "Código Aberto Auditável",
      p3Desc: "Licença GPL-3.0-or-later, zero rastreamento e build contínuo no GitHub Actions com suíte de testes automatizados (C++ / CalDAV / Sync / UI).",
      compatEyebrow: "Compatibilidade",
      compatTitle: "Feito para Plasma 6, pronto para qualquer Linux moderno",
      cColComponent: "Camada / Integração",
      cColDetails: "Suporte Verificado",
      cRowDesktop: "KDE Plasma 6 nativo em Wayland e X11 (estilo org.kde.desktop e ícones Breeze) + bandeja e atalho global Meta+Alt+N.",
      cRowNextcloud: "Nextcloud Tasks (CalDAV VTODO compatível com Tasks.org / DAVx⁵), Calendário de Rotina (VEVENT) e Nextcloud Notes REST API.",
      cRowAgendaLabel: "Agenda Externa",
      cRowAgendaVal: "Leitura somente-leitura de calendários locais .ics e integração opcional passiva com KOrganizer / Akonadi.",
      cRowLangLabel: "Idiomas",
      cRowLangVal: "Inglês e Português do Brasil (pt_BR) 100% traduzidos via KDE ki18n.",
      instEyebrow: "Instalação",
      instTitle: "Baixe o pacote para sua distribuição",
      instSub: "Pacotes verificados por checksum SHA-256 gerados automaticamente no GitHub Actions a cada release.",
      tabSource: "Código-fonte (CMake)",
      fpNote: "Recomendado para Ubuntu, Fedora, Debian, Mint, openSUSE e desktops não-Arch.",
      btnDownloadFlatpak: "Baixar .flatpak (v1.0.2)",
      archNote: "Instalação nativa via pacman ou compilação local usando o PKGBUILD incluído.",
      btnDownloadArch: "Baixar .pkg.tar.zst (v1.0.2)",
      srcNote: "Compilação direta com suporte completo a KDE Plasma e testes unitários.",
      btnCopy: "Copiar",
      btnCopied: "Copiado!",
      footContrib: "Contribuir",
      footSecurity: "Segurança",
      winSummary: "KTasks — Resumo Inicial",
      winMatrix: "KTasks — Matriz de Prioridades",
      themeLight: "Claro",
      themeDark: "Escuro"
    },
    en: {
      panelBadge: "KDE Plasma 6",
      navFeatures: "Features",
      navArch: "Privacy",
      navCompat: "Compatibility",
      navInstall: "Install",
      heroKicker: "KDE Community Application · v1.0.2",
      heroTitle: "Tasks, focus, and daily routine designed natively for KDE Plasma",
      heroLead: "An offline-first productivity app built with C++20, Qt 6, Kirigami, and SQLite. Your data stays locally on your device with optional Nextcloud Tasks, CalDAV, and Notes sync.",
      ctaDownload: "Download & Install",
      ctaRelease: "Release v1.0.2",
      tabSummary: "Summary",
      tabMatrix: "Matrix",
      zoomHint: "Click to enlarge",
      featEyebrow: "All-in-One Workspace",
      featTitle: "Your daily workflow inside a clean Kirigami interface",
      featSub: "Crafted to belong natively on Plasma 6, honoring your desktop theme, Breeze color scheme, fonts, and system icons.",
      f1Title: "Summary Dashboard & Weather",
      f1Desc: "Personalized greeting, weekday badge, clickable KPIs, compact 10-day Open-Meteo forecast (hourly auto-refresh), today's routine, and recent notes.",
      f2Title: "Eisenhower Matrix & Guided Triage",
      f2Desc: "Organize tasks across Q1–Q4 quadrants with drag-and-drop, smooth scrolling, deadline promotion, and distraction-free Guided Triage.",
      f3Title: "Pomodoro & Insights",
      f3Desc: "Persistent focus bar and per-task execution timer, session history, estimation accuracy, and 1d / 7d / 30d productivity charts.",
      f4Title: "Lists, Subtasks & Batch Actions",
      f4Desc: "Natural-language quick capture (EN/PT), Dolphin-style Ctrl/Shift multi-selection, atomic Undo/Redo, RRULE recurrence, and PDF/.ics export.",
      f5Title: "Routine & Encrypted Journal",
      f5Desc: "Time-blocked daily/weekly routines alongside Markdown notebooks locked locally with password-protected AES-256-GCM encryption.",
      f6Title: "Nextcloud & External Agenda",
      f6Desc: "Incremental CalDAV sync (VTODO & VEVENT) + Nextcloud Notes API, plus read-only KOrganizer / .ics events kept separate from routines.",
      archEyebrow: "Architecture & Security",
      archTitle: "Offline-first, encrypted, and zero telemetry",
      p1Title: "SQLite WAL + Local Backups",
      p1Desc: "Your database stays at ~/.local/share/ktasks/ktasks.db with automatic pre-migration snapshots. Works 100% offline with no account required.",
      p2Title: "System Keyring Credentials",
      p2Desc: "Nextcloud tokens (via Login Flow v2) and CalDAV passwords live safely inside KWallet / Secret Service via QtKeychain — never plaintext or logs.",
      p3Title: "Auditable Open Source",
      p3Desc: "Licensed under GPL-3.0-or-later with zero tracking and automated GitHub Actions builds plus unit tests across Core, Sync, and App layers.",
      compatEyebrow: "Compatibility",
      compatTitle: "Built for Plasma 6, portable across modern Linux desktops",
      cColComponent: "Layer / Integration",
      cColDetails: "Verified Support",
      cRowDesktop: "Native KDE Plasma 6 on Wayland and X11 (org.kde.desktop style & Breeze icons) with system tray and Meta+Alt+N global shortcut.",
      cRowNextcloud: "Nextcloud Tasks (CalDAV VTODO compatible with Tasks.org / DAVx⁵), Routine calendars (VEVENT), and Nextcloud Notes REST API.",
      cRowAgendaLabel: "External Agenda",
      cRowAgendaVal: "Read-only local .ics calendar parsing plus optional passive KOrganizer / Akonadi integration.",
      cRowLangLabel: "Languages",
      cRowLangVal: "English and Brazilian Portuguese (pt_BR) fully localized via KDE ki18n.",
      instEyebrow: "Installation",
      instTitle: "Download for your Linux distribution",
      instSub: "Release artifacts are built cleanly on GitHub Actions and verified with SHA-256 checksums.",
      tabSource: "From Source (CMake)",
      fpNote: "Recommended for Ubuntu, Fedora, Debian, Linux Mint, openSUSE, and non-Arch desktops.",
      btnDownloadFlatpak: "Download .flatpak (v1.0.2)",
      archNote: "Native installation via pacman or local package build using the included PKGBUILD.",
      btnDownloadArch: "Download .pkg.tar.zst (v1.0.2)",
      srcNote: "Direct Release build with full KDE Plasma integration and unit tests.",
      btnCopy: "Copy",
      btnCopied: "Copied!",
      footContrib: "Contributing",
      footSecurity: "Security",
      winSummary: "KTasks — Home Summary",
      winMatrix: "KTasks — Priority Matrix",
      themeLight: "Light",
      themeDark: "Dark"
    }
  };

  let currentLang = "pt";
  let currentScreen = "summary";

  function resolvedDark() {
    const mode = root.getAttribute("data-theme");
    if (mode === "dark") return true;
    if (mode === "light") return false;
    return window.matchMedia && window.matchMedia("(prefers-color-scheme: dark)").matches;
  }

  function syncThemeUI() {
    const isDark = resolvedDark();
    sunIcon.style.display = isDark ? "block" : "none";
    moonIcon.style.display = isDark ? "none" : "block";
    const dict = strings[currentLang];
    themeLabel.textContent = isDark ? dict.themeLight : dict.themeDark;
  }

  function applyLang(lang) {
    currentLang = lang;
    root.setAttribute("data-lang", lang);
    root.lang = lang === "pt" ? "pt-BR" : "en";
    langLabel.textContent = lang === "pt" ? "EN" : "PT";
    const dict = strings[lang];
    document.querySelectorAll("[data-i18n]").forEach((el) => {
      const key = el.getAttribute("data-i18n");
      if (dict[key] !== undefined) {
        el.textContent = dict[key];
      }
    });
    windowTitle.textContent = currentScreen === "summary" ? dict.winSummary : dict.winMatrix;
    syncThemeUI();
  }

  themeBtn.addEventListener("click", () => {
    const next = resolvedDark() ? "light" : "dark";
    root.setAttribute("data-theme", next);
    syncThemeUI();
  });

  if (window.matchMedia) {
    window.matchMedia("(prefers-color-scheme: dark)").addEventListener("change", syncThemeUI);
  }

  langBtn.addEventListener("click", () => {
    applyLang(currentLang === "pt" ? "en" : "pt");
  });

  document.querySelectorAll(".win-tab").forEach((btn) => {
    btn.addEventListener("click", () => {
      currentScreen = btn.getAttribute("data-screen");
      document.querySelectorAll(".win-tab").forEach((b) => {
        const active = b === btn;
        b.classList.toggle("active", active);
        b.setAttribute("aria-selected", active ? "true" : "false");
      });
      previewImg.src = currentScreen === "summary" ? "assets/summary.png" : "assets/matrix.png";
      windowTitle.textContent = currentScreen === "summary" ? strings[currentLang].winSummary : strings[currentLang].winMatrix;
    });
  });

  document.querySelectorAll(".inst-tab").forEach((btn) => {
    btn.addEventListener("click", () => {
      const target = btn.getAttribute("data-install");
      document.querySelectorAll(".inst-tab").forEach((b) => {
        const active = b === btn;
        b.classList.toggle("active", active);
        b.setAttribute("aria-selected", active ? "true" : "false");
      });
      document.querySelectorAll(".install-pane").forEach((pane) => {
        pane.classList.toggle("active", pane.id === "pane-" + target);
      });
    });
  });

  document.querySelectorAll(".copy-btn").forEach((btn) => {
    btn.addEventListener("click", async () => {
      const targetId = btn.getAttribute("data-copy");
      const codeEl = document.getElementById(targetId);
      if (!codeEl) return;
      try {
        await navigator.clipboard.writeText(codeEl.textContent.trim());
        btn.textContent = strings[currentLang].btnCopied;
        setTimeout(() => {
          btn.textContent = strings[currentLang].btnCopy;
        }, 1600);
      } catch (_) {}
    });
  });

  zoomBtn.addEventListener("click", () => {
    lightboxImg.src = previewImg.src;
    lightboxImg.alt = previewImg.alt;
    lightbox.hidden = false;
  });

  lightboxClose.addEventListener("click", () => {
    lightbox.hidden = true;
  });

  lightbox.addEventListener("click", (e) => {
    if (e.target === lightbox) lightbox.hidden = true;
  });

  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape" && !lightbox.hidden) lightbox.hidden = true;
  });

  applyLang("pt");
})();
