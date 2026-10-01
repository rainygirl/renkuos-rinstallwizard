#include "Strings.h"

#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Path.h>

#include <stdlib.h>
#include <string.h>

#include <map>

namespace {

struct Translation {
	const char* en;
	const char* it;
	const char* ko;
	const char* ja;
};

// Korean is written in 합니다체; Japanese in です/ます.
const Translation kTranslations[] = {
	// Drop zone and main window
	{ "Working on it ...", "Al lavoro ...", "작업 중입니다 ...",
		"処理中です ..." },
	{ "Drop a package (.hpkg) here to install it,",
		"Trascina qui un pacchetto (.hpkg) per installarlo,",
		"패키지(.hpkg)를 여기에 끌어다 놓으면 설치하고,",
		"パッケージ (.hpkg) をここにドロップするとインストールし、" },
	{ "or a program or its folder to make a package.",
		"o un programma o la sua cartella per crearne un pacchetto.",
		"프로그램이나 그 폴더를 놓으면 패키지를 만듭니다.",
		"プログラムかそのフォルダーならパッケージを作ります。" },
	{ "Install", "Installa", "설치", "インストール" },
	{ "A repository (https://github.com/owner/name), one of its releases, "
		"or a link to an .hpkg file",
		"Un repository (https://github.com/proprietario/nome), una sua "
		"release o un link a un file .hpkg",
		"저장소 주소(https://github.com/소유자/이름), 그 릴리스, 또는 .hpkg "
		"파일 링크",
		"リポジトリ (https://github.com/所有者/名前)、そのリリース、または "
		".hpkg ファイルへのリンク" },
	{ "The newest release with a package for this computer is installed.",
		"Viene installata la release più recente con un pacchetto per "
		"questo computer.",
		"이 컴퓨터에 맞는 패키지가 있는 가장 최근 릴리스를 설치합니다.",
		"このコンピューター用のパッケージがある最新のリリースを"
		"インストールします。" },
	{ "Something is still being installed. Quit anyway?",
		"Un'installazione è ancora in corso. Uscire comunque?",
		"아직 설치하는 중입니다. 그래도 종료하시겠습니까?",
		"まだインストール中です。終了しますか?" },
	{ "Keep working", "Continua", "계속 진행", "続ける" },
	{ "Quit", "Esci", "종료", "終了" },
	{ "Busy with the last one; drop it again when this is done.",
		"Sto ancora lavorando; trascinalo di nuovo quando ho finito.",
		"앞의 작업을 처리하는 중입니다. 끝난 뒤 다시 끌어다 놓으십시오.",
		"前の作業を処理中です。終わってからもう一度ドロップしてください。" },
	{ "Only the packages are installed; the other items were left.",
		"Vengono installati solo i pacchetti; gli altri elementi sono "
		"stati ignorati.",
		"패키지만 설치하고 나머지 항목은 그대로 두었습니다.",
		"パッケージだけをインストールし、ほかの項目はそのままにしました。" },
	{ "Something else is being installed. Try again when it is done.",
		"È in corso un'altra installazione. Riprova quando sarà finita.",
		"다른 설치가 진행 중입니다. 끝난 뒤 다시 시도하십시오.",
		"別のインストールが進行中です。終わってからもう一度試してください。" },
	{ "OK", "OK", "확인", "OK" },
	{ "Installs Haiku packages: drop an .hpkg, or give a GitHub address.\n"
		"Drop a program instead and it makes the package for you.",
		"Installa pacchetti Haiku: trascina un .hpkg o indica un indirizzo "
		"GitHub.\nTrascina invece un programma e ne crea il pacchetto.",
		"Haiku 패키지를 설치합니다. .hpkg를 끌어다 놓거나 GitHub 주소를 "
		"입력하십시오.\n프로그램을 끌어다 놓으면 패키지를 만들어 줍니다.",
		"Haiku のパッケージをインストールします。.hpkg をドロップするか、"
		"GitHub のアドレスを入力してください。\nプログラムをドロップすると"
		"パッケージを作ります。" },

	// Installing
	{ "Install this?\n\n%1\nWho should get it?",
		"Installare?\n\n%1\nPer chi?",
		"설치하시겠습니까?\n\n%1\n누구에게 설치하시겠습니까?",
		"インストールしますか?\n\n%1\n誰にインストールしますか?" },
	{ "Cancel", "Annulla", "취소", "キャンセル" },
	{ "Only me", "Solo per me", "현재 사용자만", "自分だけ" },
	{ "Everyone", "Tutti", "모든 사용자", "すべてのユーザー" },
	{ "Cancelled.", "Annullato.", "취소했습니다.", "キャンセルしました。" },
	{ "%1 is already installed.", "%1 è già installato.",
		"%1은(는) 이미 설치되어 있습니다.", "%1 はすでにインストールされています。" },
	{ "Installing ...", "Installazione ...", "설치하는 중 ...",
		"インストール中 ..." },
	{ "Installed.", "Installato.", "설치했습니다.", "インストールしました。" },
	{ "Nothing in it to put on the Desktop.",
		"Niente da mettere sulla Scrivania.",
		"바탕화면에 놓을 항목이 없습니다.",
		"デスクトップに置ける項目はありません。" },
	{ "Installed.\n\nAdd an icon to the Desktop as well?\n\n%1",
		"Installato.\n\nAggiungere anche un'icona sulla Scrivania?\n\n%1",
		"설치했습니다.\n\n바탕화면에도 아이콘을 만드시겠습니까?\n\n%1",
		"インストールしました。\n\nデスクトップにもアイコンを作りますか?\n\n%1" },
	{ "No", "No", "아니요", "いいえ" },
	{ "Add to Desktop", "Aggiungi alla Scrivania", "바탕화면에 추가",
		"デスクトップに追加" },
	{ "%1 cannot be installed here.\n\n%2",
		"%1 non può essere installato qui.\n\n%2",
		"%1을(를) 이 컴퓨터에 설치할 수 없습니다.\n\n%2",
		"%1 はこのコンピューターにはインストールできません。\n\n%2" },
	{ "Looking up %1 ...", "Ricerca di %1 ...", "%1을(를) 찾는 중 ...",
		"%1 を検索中 ..." },
	{ "Asking GitHub ...", "Interrogo GitHub ...", "GitHub에 묻는 중 ...",
		"GitHub に問い合わせ中 ..." },
	{ "Found %1", "Trovato %1", "찾았습니다: %1", "見つかりました: %1" },
	{ "Downloading %1", "Download di %1", "%1 내려받는 중",
		"%1 をダウンロード中" },
	{ "Download failed: %1", "Download non riuscito: %1",
		"내려받지 못했습니다: %1", "ダウンロードに失敗しました: %1" },
	{ "Could not download %1.\n\n%2", "Impossibile scaricare %1.\n\n%2",
		"%1을(를) 내려받지 못했습니다.\n\n%2",
		"%1 をダウンロードできませんでした。\n\n%2" },
	{ "Making a package of %1 ...", "Creazione del pacchetto di %1 ...",
		"%1의 패키지를 만드는 중 ...", "%1 のパッケージを作成中 ..." },
	{ "Making the package ...", "Creazione del pacchetto ...",
		"패키지를 만드는 중 ...", "パッケージを作成中 ..." },
	{ "Saved %1", "Salvato %1", "저장했습니다: %1", "保存しました: %1" },
	{ "Made in %1:\n\n%2", "Creato in %1:\n\n%2", "%1에 만들었습니다.\n\n%2",
		"%1 に作成しました。\n\n%2" },

	// Make-a-package window
	{ "Make a package", "Crea un pacchetto", "패키지 만들기",
		"パッケージの作成" },
	{ "Package \"%1\"", "Pacchetto \"%1\"", "\"%1\" 패키지",
		"パッケージ \"%1\"" },
	{ "Name:", "Nome:", "이름:", "名前:" },
	{ "Package name:", "Nome del pacchetto:", "패키지 이름:", "パッケージ名:" },
	{ "Version:", "Versione:", "버전:", "バージョン:" },
	{ "Summary:", "Descrizione:", "요약:", "概要:" },
	{ "Your name:", "Il tuo nome:", "만든 사람:", "作成者:" },
	{ "E-mail:", "E-mail:", "이메일:", "メール:" },
	{ "Source link:", "Link al sorgente:", "소스 링크:", "ソースのリンク:" },
	{ "Where the source code is, e.g. a GitHub repository",
		"Dove si trova il codice sorgente, ad es. un repository GitHub",
		"소스 코드가 있는 곳(예: GitHub 저장소)",
		"ソースコードの場所 (例: GitHub リポジトリ)" },
	{ "License:", "Licenza:", "라이선스:", "ライセンス:" },
	{ "Install as:", "Installa come:", "설치 형태:", "インストール形式:" },
	{ "Install for:", "Installa per:", "설치 대상:", "インストール先:" },
	{ "Application (in the Deskbar menu)",
		"Applicazione (nel menu della Deskbar)",
		"응용 프로그램 (Deskbar 메뉴)", "アプリケーション (Deskbar メニュー)" },
	{ "Preference (in the Preferences menu)",
		"Preferenza (nel menu Preferenze)", "환경설정 (Preferences 메뉴)",
		"設定 (Preferences メニュー)" },
	{ "Command-line tool", "Strumento da riga di comando", "명령줄 도구",
		"コマンドラインツール" },
	{ "Everyone (system)", "Tutti (sistema)", "모든 사용자 (시스템)",
		"すべてのユーザー (システム)" },
	{ "Only me (home)", "Solo per me (home)", "현재 사용자만 (홈)",
		"自分だけ (ホーム)" },
	{ "Choose" "\xE2\x80\xA6", "Scegli" "\xE2\x80\xA6", "선택" "\xE2\x80\xA6",
		"選択" "\xE2\x80\xA6" },
	{ "Remove", "Rimuovi", "제거", "削除" },
	{ "Save to:", "Salva in:", "저장 위치:", "保存先:" },
	{ "Make package", "Crea pacchetto", "패키지 만들기", "パッケージを作成" },
	{ "Make and install", "Crea e installa", "만들고 설치",
		"作成してインストール" },
	{ "Drop the source folder on this window to add it as a _source package.",
		"Trascina qui la cartella dei sorgenti per aggiungerla come "
		"pacchetto _source.",
		"소스 폴더를 이 창에 끌어다 놓으면 _source 패키지로 함께 만듭니다.",
		"ソースフォルダーをこのウィンドウにドロップすると _source "
		"パッケージとして追加します。" },
	{ "Source folder:", "Cartella sorgenti:", "소스 폴더:",
		"ソースフォルダー:" },
	{ "(none)", "(nessuna)", "(없음)", "(なし)" },
	{ "Choose the source folder", "Scegli la cartella dei sorgenti",
		"소스 폴더 선택", "ソースフォルダーを選択" },
	{ "Give the program a name.", "Dai un nome al programma.",
		"프로그램 이름을 입력하십시오.", "プログラムの名前を入力してください。" },
	{ "The package name may hold only small letters, digits and '_', and "
		"must start with a letter.",
		"Il nome del pacchetto può contenere solo lettere minuscole, cifre "
		"e '_', e deve iniziare con una lettera.",
		"패키지 이름에는 영문 소문자, 숫자, '_'만 쓸 수 있고, 영문자로 "
		"시작해야 합니다.",
		"パッケージ名には英小文字、数字、'_' だけが使え、英字で始める必要が"
		"あります。" },
	{ "The version may hold letters, digits, '.', '_' and '~' (like 1.0 or "
		"2.3.1), but no '-'.",
		"La versione può contenere lettere, cifre, '.', '_' e '~' (come 1.0 "
		"o 2.3.1), ma non '-'.",
		"버전에는 영문자, 숫자, '.', '_', '~'를 쓸 수 있고(예: 1.0, 2.3.1), "
		"'-'는 쓸 수 없습니다.",
		"バージョンには英字、数字、'.'、'_'、'~' が使えます (例: 1.0、"
		"2.3.1)。'-' は使えません。" },
	{ "Put your name in: a package says who made it.",
		"Inserisci il tuo nome: un pacchetto dice chi lo ha creato.",
		"이름을 입력하십시오. 패키지에는 만든 사람이 기록됩니다.",
		"名前を入力してください。パッケージには作成者が記録されます。" },
	{ "Cannot save into %1.", "Impossibile salvare in %1.",
		"%1에 저장할 수 없습니다.", "%1 に保存できません。" },

	// Package tools
	{ "This is a source package: it holds source code, not a program.",
		"Questo è un pacchetto sorgente: contiene codice, non un programma.",
		"소스 패키지입니다. 프로그램이 아니라 소스 코드가 들어 있습니다.",
		"ソースパッケージです。プログラムではなくソースコードが入って"
		"います。" },
	{ "This package is for %1, and this system is %2.",
		"Questo pacchetto è per %1, e questo sistema è %2.",
		"이 패키지는 %1용이고, 이 시스템은 %2입니다.",
		"このパッケージは %1 用で、このシステムは %2 です。" },
	{ "Desktop already has \"%1\"; left as it is.",
		"La Scrivania ha già \"%1\"; lasciato com'è.",
		"바탕화면에 이미 \"%1\"이(가) 있어 그대로 두었습니다.",
		"デスクトップにはすでに \"%1\" があるため、そのままにしました。" },
	{ "Added \"%1\" to the Desktop.", "Aggiunto \"%1\" alla Scrivania.",
		"바탕화면에 \"%1\"을(를) 추가했습니다.",
		"デスクトップに \"%1\" を追加しました。" },
	{ "%1 is not a package Haiku can read.",
		"%1 non è un pacchetto leggibile da Haiku.",
		"%1은(는) Haiku가 읽을 수 있는 패키지가 아닙니다.",
		"%1 は Haiku が読めるパッケージではありません。" },
	{ "%1 has no package name.", "%1 non ha un nome di pacchetto.",
		"%1에 패키지 이름이 없습니다.", "%1 にはパッケージ名がありません。" },
	{ "pkgman could not install it: %1",
		"pkgman non è riuscito a installarlo: %1",
		"pkgman이 설치하지 못했습니다: %1",
		"pkgman がインストールできませんでした: %1" },
	{ "There is no program in \"%1\": no file in it is marked executable.",
		"Non c'è alcun programma in \"%1\": nessun file è eseguibile.",
		"\"%1\"에 프로그램이 없습니다. 실행 파일로 표시된 파일이 없습니다.",
		"\"%1\" にはプログラムがありません。実行可能なファイルがありません。" },
	{ "Architecture: %1", "Architettura: %1", "아키텍처: %1",
		"アーキテクチャ: %1" },
	{ "Copying %1 ...", "Copia di %1 ...", "%1 복사하는 중 ...",
		"%1 をコピー中 ..." },
	{ "Could not copy \"%1\".", "Impossibile copiare \"%1\".",
		"\"%1\"을(를) 복사하지 못했습니다.", "\"%1\" をコピーできませんでした。" },
	{ "Could not make the Deskbar entry.",
		"Impossibile creare la voce della Deskbar.",
		"Deskbar 항목을 만들지 못했습니다.",
		"Deskbar の項目を作成できませんでした。" },
	{ "Could not write the package description.",
		"Impossibile scrivere la descrizione del pacchetto.",
		"패키지 정보를 쓰지 못했습니다.",
		"パッケージ情報を書き込めませんでした。" },
	{ "Creating %1 ...", "Creazione di %1 ...", "%1 만드는 중 ...",
		"%1 を作成中 ..." },
	{ "Copying the source folder ...", "Copia della cartella sorgenti ...",
		"소스 폴더를 복사하는 중 ...", "ソースフォルダーをコピー中 ..." },
	{ "Could not copy the source folder.",
		"Impossibile copiare la cartella sorgenti.",
		"소스 폴더를 복사하지 못했습니다.",
		"ソースフォルダーをコピーできませんでした。" },
	{ "Could not write the source package description.",
		"Impossibile scrivere la descrizione del pacchetto sorgente.",
		"소스 패키지 정보를 쓰지 못했습니다.",
		"ソースパッケージ情報を書き込めませんでした。" },
	{ "Making the package failed: %1",
		"Creazione del pacchetto non riuscita: %1",
		"패키지를 만들지 못했습니다: %1",
		"パッケージを作成できませんでした: %1" },

	// GitHub
	{ "GitHub sent something unreadable: %1",
		"GitHub ha inviato una risposta illeggibile: %1",
		"GitHub의 응답을 읽을 수 없습니다: %1",
		"GitHub の応答を読めません: %1" },
	{ "Type a GitHub address first.", "Scrivi prima un indirizzo GitHub.",
		"먼저 GitHub 주소를 입력하십시오.",
		"先に GitHub のアドレスを入力してください。" },
	{ "That is not a GitHub repository address. Use "
		"https://github.com/owner/repository, or a link to an .hpkg file.",
		"Non è l'indirizzo di un repository GitHub. Usa "
		"https://github.com/proprietario/repository o un link a un file "
		".hpkg.",
		"GitHub 저장소 주소가 아닙니다. https://github.com/소유자/저장소 "
		"형식이나 .hpkg 파일 링크를 입력하십시오.",
		"GitHub リポジトリのアドレスではありません。"
		"https://github.com/所有者/リポジトリ か .hpkg ファイルへのリンクを"
		"入力してください。" },
	{ "GitHub does not know %1.", "GitHub non conosce %1.",
		"GitHub에서 %1을(를) 찾을 수 없습니다.",
		"GitHub に %1 は見つかりません。" },
	{ "GitHub refused for now (%1). Try again in a while.",
		"GitHub per ora rifiuta (%1). Riprova tra un po'.",
		"GitHub가 지금은 요청을 거절했습니다(%1). 잠시 뒤 다시 "
		"시도하십시오.",
		"GitHub が現在リクエストを拒否しています (%1)。しばらくしてから"
		"試してください。" },
	{ "GitHub answered %1: %2", "GitHub ha risposto %1: %2",
		"GitHub 응답 %1: %2", "GitHub の応答 %1: %2" },
	{ "%1 has no releases on GitHub.", "%1 non ha release su GitHub.",
		"%1에는 GitHub 릴리스가 없습니다.",
		"%1 には GitHub のリリースがありません。" },
	{ "No release of %1 has a package (.hpkg) for %2.",
		"Nessuna release di %1 ha un pacchetto (.hpkg) per %2.",
		"%1의 어느 릴리스에도 %2용 패키지(.hpkg)가 없습니다.",
		"%1 のどのリリースにも %2 用のパッケージ (.hpkg) がありません。" },

	// Network
	{ "Cannot find %1 on the network.", "Impossibile trovare %1 in rete.",
		"네트워크에서 %1을(를) 찾을 수 없습니다.",
		"ネットワーク上で %1 が見つかりません。" },
	{ "Cannot connect to %1.", "Impossibile connettersi a %1.",
		"%1에 연결할 수 없습니다.", "%1 に接続できません。" },
	{ "The certificate of %1 is not trusted: %2. Is the system clock right?",
		"Il certificato di %1 non è attendibile: %2. L'orologio di sistema "
		"è corretto?",
		"%1의 인증서를 신뢰할 수 없습니다: %2. 시스템 시계가 맞는지 "
		"확인하십시오.",
		"%1 の証明書を信頼できません: %2。システムの時計は正しいですか?" },
	{ "The secure connection to %1 failed.",
		"La connessione sicura con %1 non è riuscita.",
		"%1과(와) 보안 연결을 맺지 못했습니다.",
		"%1 との安全な接続に失敗しました。" },
	{ "%1 did not answer properly.", "%1 non ha risposto correttamente.",
		"%1이(가) 제대로 응답하지 않았습니다.",
		"%1 から正しい応答がありませんでした。" },
	{ "The connection to %1 broke off.", "La connessione con %1 si è interrotta.",
		"%1과(와)의 연결이 끊어졌습니다.", "%1 との接続が切れました。" },
	{ "The server answered %1.", "Il server ha risposto %1.",
		"서버 응답: %1", "サーバーの応答: %1" },
	{ "Cannot write %1. Is the disk full?",
		"Impossibile scrivere %1. Il disco è pieno?",
		"%1에 쓸 수 없습니다. 디스크가 가득 찼는지 확인하십시오.",
		"%1 に書き込めません。ディスクがいっぱいではありませんか?" },
	{ "Too many redirects.", "Troppi reindirizzamenti.",
		"리디렉션이 너무 많습니다.", "リダイレクトが多すぎます。" },
};


std::string
DetectLanguage()
{
	const char* forced = getenv("RIW_LANG");
	if (forced != NULL && forced[0] != '\0')
		return std::string(forced, 0, 2);

	// The Locale preferences' own settings file. BLocaleRoster would read the
	// same list, but it lives in liblocale on older Haiku and in libbe on
	// newer, and one binary should run on both.
	BPath path;
	BMessage languages;
	BFile file;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK
		&& path.Append("Locale settings") == B_OK
		&& file.SetTo(path.Path(), B_READ_ONLY) == B_OK
		&& languages.Unflatten(&file) == B_OK) {
		const char* language;
		for (int32 i = 0; languages.FindString("language", i, &language) == B_OK;
				i++) {
			std::string code(language, 0, 2);
			if (code == "en" || code == "it" || code == "ko" || code == "ja")
				return code;
		}
	}
	return "en";
}


const std::string&
Language()
{
	static const std::string language = DetectLanguage();
	return language;
}


const std::map<std::string, const char*>&
Table()
{
	static std::map<std::string, const char*> table;
	if (!table.empty() || Language() == "en")
		return table;
	for (size_t i = 0; i < sizeof(kTranslations) / sizeof(kTranslations[0]);
			i++) {
		const Translation& t = kTranslations[i];
		const char* text = Language() == "it" ? t.it
			: Language() == "ko" ? t.ko : t.ja;
		table[t.en] = text;
	}
	return table;
}

} // namespace


const char*
T(const char* english)
{
	const std::map<std::string, const char*>& table = Table();
	std::map<std::string, const char*>::const_iterator found
		= table.find(english);
	return found == table.end() ? english : found->second;
}


std::string
TF(const char* english, const std::string& a1, const std::string& a2,
	const std::string& a3)
{
	std::string text = T(english);
	const std::string* args[] = { &a1, &a2, &a3 };
	for (size_t pos = 0; (pos = text.find('%', pos)) != std::string::npos;) {
		if (pos + 1 < text.size() && text[pos + 1] >= '1'
			&& text[pos + 1] <= '3') {
			const std::string& value = *args[text[pos + 1] - '1'];
			text.replace(pos, 2, value);
			pos += value.size();
		} else
			pos++;
	}
	return text;
}


const char*
CurrentLanguage()
{
	return Language().c_str();
}
