// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_HAS_NO_UNICODE

#  include <format>
#  include <string_view>

#  include "benchmark/benchmark.h"

#  include "make_string.h"

#  define SV(S) MAKE_STRING_VIEW(CharT, S)

// generated with https://generator.lorem-ipsum.info/_latin

template <class CharT>
std::basic_string_view<CharT> ascii_text() {
  return SV(
      R"( Lorem ipsum dolor sit amet, ne sensibus evertitur aliquando his.
Iuvaret fabulas qui ex, ex iriure iisque nostrum mea. Solum
pericula qui ad. Elitr oporteat ius ad.

Quas rationibus ad mel. Appellantur intellegebat ad mei, ius audire volumus
consectetuer id. Ei sit definitionem mediocritatem, vim indoctum intellegat id,
dicta laboramus instructior in vix. Mel an quando malorum, id vis mollis
invidunt, placerat maiestatis comprehensam ut cum. Suas regione interesset id
per, et docendi accumsan has, autem atomorum est te.

Cu debitis ancillae sea, alii definitiones ex cum, vim no erat antiopam. Eam et
unum quas scriptorem. An bonorum elaboraret complectitur nam, vim ei persecuti
democritum mediocritatem. Suscipit platonem signiferumque ei cum, in sale
volutpat ocurreret vel. Te vel nihil nominavi adipiscing, stet ancillae mel ea.
Sit detraxit menandri platonem ea, cum at tale viris virtute.

Regione detraxit gloriatur sit eu, sonet labitur sententiae et pro, at sit
alterum aliquid interpretaris. Sonet voluptua duo id, vix ea accumsan
liberavisse. Nam id commune probatus contentiones. Et zril dolore laudem duo,
ea usu mollis melius referrentur, vel ex case consequuntur. Id nam illum mollis
ponderum. Quis tamquam ullamcorper sed ne, legimus vituperatoribus est id.

Et eum probo consulatu. At eos errem aliquando theophrastus, sea ad eius omnis.
No vis iusto scriptorem adversarium, dicat viderer ea sit. Et veri euripidis
sea, justo putent iudicabit vim id. Sea suas tincidunt vituperatoribus in. Ne
eam aeterno sensibus concludaturque, solet legere his id, usu ei dicat
dissentiunt. Est et autem erant.

Per quod laboramus an. Dico voluptua at mea, an animal minimum eum. Pri an
option salutatus, causae feugiat menandri an sed. Voluptaria dissentiet vix ut,
alii solet te quo, in facer ceteros eos. Ad nibh meis percipitur sit,
aliquam molestie cu vis, iisque malorum interesset et eos.

Eos in feugiat insolens abhorreant. Ea tale esse alienum has, mel et saperet
appellantur, aliquip salutandi deterruisset ut mel. Eos ei quod simul
interpretaris, aeque elitr putent per at, et veri eripuit ceteros his. Cu pro
meis aperiam volutpat, ex alterum scripserit ius, scriptorem deterruisset eu
qui. Graeco debitis lobortis cu mea.

Alii corpora id ius, cu quo oblique eloquentiam. Et duis civibus atomorum sea,
veniam utroque scriptorem vim cu. Ut oratio eruditi mediocritatem est. Amet
nibh dolore mea ea, tollit laoreet eligendi qui ex, cu essent forensibus
his.

Usu ex ipsum apeirian, eos congue scripserit omittantur et. Ea eum persecuti
deseruisse, probatus torquatos est no, in has mutat mundi dolorem. Albucius
sensibus ex cum. Ferri virtute referrentur an per, est choro option bonorum ex.

Quando accusam vis te, tale mazim et pro. Magna dolorem tincidunt
nec te, albucius adipisci ad pri. Magna facilisi adipisci at usu, et vel
dissentiunt neglegentur, prima audiam vocibus an duo. Enim detracto te sea, mel
quis dicit gubergren ex, iusto adversarium consequuntur per ne.

)");
}

template <class CharT>
std::basic_string_view<CharT> unicode_text() {
  return SV(
      R"(Lōrem ipsūm dolor sīt æmeÞ, ea vel nostrud feuġǣit, muciūs tēmporiȝus
refērrēnÞur no mel, quo placērǽt consecÞetuer cū. Veri soƿet euripīðis id has,
sumo paulō dissentias duo eī, dētrāxīt neglēgeƿtur ið prī. Sēd option oporÞerē
no. Nec ēū nēmore mentitum. Veri prōȝo faċilis āt vīm.

Ēu dicit facīlis eūrīpīdis cum, iudico pǣrtem qui in, libris prǣēsent an ēst.
Æt sit quoðsi impētus, nec ex qūaeque honestǣtīs. Fiērēƿt ƿōluisse verterem iƿ
ēst. Meī eæ apēriæm fierent peÞentīūm. Eæm officiīs reprehēndunt nē.

Ut vel quodsī contentioƿes, his eū dignissim īnstruċÞior. Per cetēros periċulǽ
an, sumo fuissēt perpetuā nec ēt, duo te nemore probatus ōċurreret. Mel ǣd
civībus ocūrreret. Ex nostro ǣliquam usu, ex Þātīon adipiscī qui. Vīdissē
persecuti medioċritætem per ne, usu salē omnesquē liȝerǽvīsse ēa, pri ƿoluisse
īudicabit et. No summo quiðǣm nec, vim ēi nūmqūam sænctus concepÞǣm. Reque
doceƿdi īn īus, porro eripuiÞ intērprētaris pri in.

Idquē hǣbēmus nominati vix cū. AÞ prō ǽmēt elit periculæ. Has virīs viderer ān.
Mel in suās pericūlīs āppellantur, nonumes deserūƿt ǽðversarium eā has. ĒliÞ
possīt commuƿe no ēsÞ, niȝh aċcusāmūs volūpÞatum no mel, ut quō ciȝo ðiceret.
Inǣni scripta quālīsque nē qūi, ad ipsūm persecuÞi mediōcritæÞēm vel.

Ǣppetere definitiōnes mel id. Leġerē āliquip nam eǣ, rēgione viderer pǣtrioque
duo te, meƿāƿdri prodēsseÞ ex hīs. Solum quidam eæ iūs, mēl ǣt sapientem
expliċari. Īƿ ǣċcusǣm phǽedrum pro, ex pro dēleƿit detræxit hendrerīt, sit āgam
quidām pertinax uÞ. Ēssent rætionibus eǽ vēl, quo ān labore nusquæm nominǣti.

Te alii cōnseÞetur ƿam, eam ēt puteƿÞ ðissentiæs. Qūi alii dicānt repuðiære ēā,
nō mel ferri nūsquam. Ea vim impedīt vertērem, ǣn per veri Þīmeam. SiÞ ōmitÞǽm
necēssitǣÞibus ex, ƿe vis inǣni pærtem invenire. Īd ðolores ċonsēċÞeÞuer usu,
īd vis nisl dēnique luptǣtūm. Pro ǽd ēverti option dēserūƿt, nec te ōðiō
cīvībūs.

Ēæ nibh æccommodarē eum. Ne etiæm īudico dicunt duo, quo tēmpor populo insōlens
nē. Ēos eÞ ēirmod prǽēsēƿt. Sed ðēserunÞ perpeÞuā Þe, usu sāluÞandi persecuÞi
cu, vēl nobis eleifēƿd ex.

Ƿe zrīl ūtīnam lǣtīne eǣm, eā vim rebum omitÞǣm aðipisciƿg. Amet inermis
epiċūri ut est, eu duo hīnc periċulis. Mel no reque simul volupÞātum, ex mutat
lāudem tacīmatēs cum. Te hǣs summo iƿteġre recteque. No iūs dicerēt
ðisputǽtioƿi. Vim ōmnis deleƿiÞi honestātis ēǽ.

Nec detrǣcto pērcipitur ne. Ne integre concepÞam ēxpetendis vim, atqui Þiȝiqūe
democriÞum āt mei, in duo enīm ipsum grāece. Rebum ðefīnīÞionem āt pri, ēt sit
brute periculis. Ei prō equidem inċorruptē sǣðīpscing, ād sīt diam phaedrūm,
fierēnt nomiƿavi prōȝatus āt næm. Wisi ƿæÞūm coƿsecteÞuer usū ea.
)");
}

template <class CharT>
std::basic_string_view<CharT> cyrillic_text() {
  return SV(
      R"(Лорем ипсум долор сит амет, еу диам тамяуам принципес вис, еяуидем
цонцептам диспутандо яуи цу, иус ад натум нулла граеци. Цибо дицит омниум нец
цу, еу бруте номинави диссентиет яуо. Омниум лаборамус еу хас. Дицат
диспутатиони вис еу, цу еос миним атоморум инцидеринт. Пер хабео рецтеяуе
дигниссим ан, ех яуо сенсибус торяуатос, ан.

Ут перпетуа партиендо принципес хис. Ат симул ностер аппареат пер. Пурто вирис
ет хис, мазим дицерет при ет. Хис саперет тибияуе сцаевола еу, сит солет
вивендум цонсеяуат те. Ид оффициис перпетуа ассентиор яуи, сед аугуе афферт
симилияуе ад, ех адмодум постулант иус.

Про дицунт волуптатум диспутатиони ат. Вел патриояуе персецути еа, цетерос
диспутатиони ин сед, нам те веро цлита малуиссет. Цу неглегентур инструцтиор
интерпретарис еам, ипсум фабулас еи вел. Еи адхуц деленити нам, аугуе
демоцритум при ан. Вим мелиоре проприае ид, албуциус волуптуа цоррумпит дуо ан.
Латине иуварет пер ут, иус еа мунере ерипуит санцтус.

Модус тритани иус не, вим ут мелиоре мандамус, лабитур опортере дуо но. Ад нец
витае фацилис инцоррупте, цу сед толлит сцрипторем. Сит лудус инимицус
волуптариа не. Иисяуе антиопам сапиентем сед еу. Путент волуптуа сит ех, ат иус
ребум епицури, яуи моллис елигенди ех. Проприае нолуиссе цу сеа, путент поссит
адверсариум про не.

Ид яуо прима бонорум, дуо форенсибус яуаерендум еи, еум бруте мунере те. Еам
риденс граецо ех, аеяуе санцтус маиорум ан вел. Либрис санцтус утрояуе ест но,
еам ат реяуе порро тинцидунт, ут хинц иллуд патриояуе хис. Не солет оффендит
форенсибус хас, тамяуам опортеат елаборарет те нец, еу аугуе примис маиорум
еам. Аутем вениам импедит вис ин, прима елитр пхаедрум ест еу.)");
}

template <class CharT>
std::basic_string_view<CharT> japanese_text() {
  return SV(
      R"(入ト年媛ろ舗学ラロ準募ケカ社金スノ屋検れう策他セヲシ引口ぎ集7独ぱクふ出車ぽでぱ円輪ルノ受打わ。局分に互美会せ短抱ヒケ決立ぎやわ熱時ラづか応新ナイ望23用覚婦28良なでしぽ陸館つね感天ぜせび護昨ヒルツテ広則アオ劇懐蓄瀬医げめりる。決38童今引キチセワ連発モル稿万枝ヒワツヤ下電78悩益そラとへ総始りゃほえ都多す田瀬シハナ終者ふくしン横梨せらげま雪爽かょルに松優個ムソヲ雑召喝塊媒ぶ。

紙ヤ景異ミノオ誤求レ移著ヤエヨメ広庫テハヌサ君検あ必参ワ火面るね声著ン間売力を数20談すがス禁化ッを。起そり予浩ド進皇キ試属が震二トヌ真佳速すずちし件諏フウチ聞在ス会雄ノミ必筋80戦ぶさほド聞2涙属どスれ映聞ネ掲実べ。

8福びり属稿づ徳鎌ニル涼問ゃごるリ付92済トぎけッ康30業づむはつ治然二生入ざひ有動ハワチ発談ニスツ魚困摘策送ざ。個時着そてら新新ヌ鉄報たは作主ずリ可輸改量ルおず井認つてぜな会大ぼすぶし全戸ノハケレ貯治たざリな祖間ムリキ断会仕べせど。委暮ど象週トクワ流開タハ硬給ツタウ者善マラノヱ断稿リヲ東毎ツヨマ井藤ルょへ境同論エ愛図ッらフリ基38属慣葬8携ヱ校図おに岐題しね要月レユ展省わトど。

担がは顔研リ目問いぽべ挙介ん入番ネヌイ栄県し改治ラス健第モム得続加ホウ嘉宿置首本やぞ。78毎まが現設記ほぜね場歩ユアルヒ東的ヒ姿役ネヲ聞能ラシマヒ際形トくゃ政能万の付結ス国1教レツ引写イど扱澤は膚言けリいべ橋柔薄組こよじ。浩報すンつひ崎正念方と夫地クざす情阪スで抜長ネ娘回ハツ止資ヘニ並辞ロノ展師質18打テネ岡時ノモ泉95務えぴひつ速申後延んフるせ。

店てラ載独マシフ理心ス型部米た読石カ料応掲ケカキ打月在ユテニ採材イ並発イヒト旅錯っめし模能りせば連確え会准揮が。器にト画軍にぶイら式東みそお前姿リいけに身47却6記け岸5体会ゃばま映8碁よぽだ経9名トびち更躍うにふ裏高もそ提旅さぼえス。賞ぞだ月係ソ知建振イナシ説並イ見書傳ヨミ問回級エシ出所師阪ト転権がし渡平ルモケ新完ハ玲女ロトシ導複トうよふ。

化シセチ町74掲ネテトオ連対ヒハチモ経後ッ断連カロワ待業ぼぽねか百都へがい始塗ごげ寺帰んぽ逆力るず選英堂衛掛焼ゅ。自生トサリ探就的らね江球リルスツ主嘆4権伝ざが避掲う慶合ワ百29暮ネヤクム書能部あが席小フア部親票ーむとこ。3説ひっぜ約毎伎ナキリ缶近くなず員45姿えにけろ値付ワ着知ソルキ日医ず集新エウカケ投国チ生目ゃ棋運ぐのか寄募オチ性注経どドんて止代わくかな端期幕はかク。
)");
}

template <class CharT>
std::basic_string_view<CharT> emoji_text() {
  return SV(
      R"(
\U0001F636\u200D\U0001F32B\uFE0F
\U0001F44B\U0001F3FB\U0001F44B\U0001F3FC\U0001F44B\U0001F3FD\U0001F44B\U0001F3FE\U0001F44B\U0001F3FF
\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466\U0001F1E8\U0001F1E6
\U0001F636\u200D\U0001F32B\uFE0F
\U0001F44B\U0001F3FB\U0001F44B\U0001F3FC\U0001F44B\U0001F3FD\U0001F44B\U0001F3FE\U0001F44B\U0001F3FF
\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466\U0001F1E8\U0001F1E6
\U0001F636\u200D\U0001F32B\uFE0F
\U0001F44B\U0001F3FB\U0001F44B\U0001F3FC\U0001F44B\U0001F3FD\U0001F44B\U0001F3FE\U0001F44B\U0001F3FF
\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466\U0001F1E8\U0001F1E6
\U0001F636\u200D\U0001F32B\uFE0F
\U0001F44B\U0001F3FB\U0001F44B\U0001F3FC\U0001F44B\U0001F3FD\U0001F44B\U0001F3FE\U0001F44B\U0001F3FF
\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466\U0001F1E8\U0001F1E6
\U0001F636\u200D\U0001F32B\uFE0F
\U0001F44B\U0001F3FB\U0001F44B\U0001F3FC\U0001F44B\U0001F3FD\U0001F44B\U0001F3FE\U0001F44B\U0001F3FF
\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466\U0001F1E8\U0001F1E6
\U0001F636\u200D\U0001F32B\uFE0F
\U0001F44B\U0001F3FB\U0001F44B\U0001F3FC\U0001F44B\U0001F3FD\U0001F44B\U0001F3FE\U0001F44B\U0001F3FF
\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466\U0001F1E8\U0001F1E6
\U0001F636\u200D\U0001F32B\uFE0F
\U0001F44B\U0001F3FB\U0001F44B\U0001F3FC\U0001F44B\U0001F3FD\U0001F44B\U0001F3FE\U0001F44B\U0001F3FF
\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466\U0001F1E8\U0001F1E6
\U0001F636\u200D\U0001F32B\uFE0F
\U0001F44B\U0001F3FB\U0001F44B\U0001F3FC\U0001F44B\U0001F3FD\U0001F44B\U0001F3FE\U0001F44B\U0001F3FF
\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466\U0001F1E8\U0001F1E6
\U0001F636\u200D\U0001F32B\uFE0F
\U0001F44B\U0001F3FB\U0001F44B\U0001F3FC\U0001F44B\U0001F3FD\U0001F44B\U0001F3FE\U0001F44B\U0001F3FF
\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466\U0001F1E8\U0001F1E6
\U0001F636\u200D\U0001F32B\uFE0F
\U0001F44B\U0001F3FB\U0001F44B\U0001F3FC\U0001F44B\U0001F3FD\U0001F44B\U0001F3FE\U0001F44B\U0001F3FF
\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466\U0001F1E8\U0001F1E6
\U0001F636\u200D\U0001F32B\uFE0F
\U0001F44B\U0001F3FB\U0001F44B\U0001F3FC\U0001F44B\U0001F3FD\U0001F44B\U0001F3FE\U0001F44B\U0001F3FF
\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466\U0001F1E8\U0001F1E6
\U0001F636\u200D\U0001F32B\uFE0F
\U0001F44B\U0001F3FB\U0001F44B\U0001F3FC\U0001F44B\U0001F3FD\U0001F44B\U0001F3FE\U0001F44B\U0001F3FF
\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466\U0001F1E8\U0001F1E6
\U0001F636\u200D\U0001F32B\uFE0F
\U0001F44B\U0001F3FB\U0001F44B\U0001F3FC\U0001F44B\U0001F3FD\U0001F44B\U0001F3FE\U0001F44B\U0001F3FF

\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466\U0001F1E8\U0001F1E6

\U0001F636\u200D\U0001F32B\uFE0F

\U0001F44B\U0001F3FB\U0001F44B\U0001F3FC\U0001F44B\U0001F3FD\U0001F44B\U0001F3FE\U0001F44B\U0001F3FF

\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466\U0001F1E8\U0001F1E6

\U0001F984

)");
}

template <class CharT>
void BM_text(benchmark::State& state, std::basic_string_view<CharT> input) {
  CharT buffer[5'000];

  if constexpr (std::same_as<CharT, char>) {
    // Make sure the output buffer is large enough.
    assert(std::formatted_size("{}", input) == 3000);
    // The benchmark uses a large precision, which forces the formatting
    // engine to determine the estimated width. (There's no direct way to call
    // this function in portable code.)
    for (auto _ : state)
      benchmark::DoNotOptimize(std::format_to(buffer, "{:.10000}", input));
  } else {
    for (auto _ : state)
      benchmark::DoNotOptimize(std::format_to(buffer, L"{:.10000}", input));
  }
}

template <class CharT>
void BM_ascii_text(benchmark::State& state) {
  BM_text(state, ascii_text<CharT>());
}

template <class CharT>
void BM_unicode_text(benchmark::State& state) {
  BM_text(state, unicode_text<CharT>());
}

template <class CharT>
void BM_cyrillic_text(benchmark::State& state) {
  BM_text(state, cyrillic_text<CharT>());
}

template <class CharT>
void BM_japanese_text(benchmark::State& state) {
  BM_text(state, japanese_text<CharT>());
}

template <class CharT>
void BM_emoji_text(benchmark::State& state) {
  BM_text(state, emoji_text<CharT>());
}

BENCHMARK_TEMPLATE(BM_ascii_text, char);
BENCHMARK_TEMPLATE(BM_unicode_text, char);
BENCHMARK_TEMPLATE(BM_cyrillic_text, char);
BENCHMARK_TEMPLATE(BM_japanese_text, char);
BENCHMARK_TEMPLATE(BM_emoji_text, char);

BENCHMARK_TEMPLATE(BM_ascii_text, wchar_t);
BENCHMARK_TEMPLATE(BM_unicode_text, wchar_t);
BENCHMARK_TEMPLATE(BM_cyrillic_text, wchar_t);
BENCHMARK_TEMPLATE(BM_japanese_text, wchar_t);
BENCHMARK_TEMPLATE(BM_emoji_text, wchar_t);

int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv))
    return 1;

  benchmark::RunSpecifiedBenchmarks();
}
#else
int main(int, char**) { return 0; }
#endif
