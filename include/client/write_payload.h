#pragma once

#include <rte_cycles.h>
#include <rte_memcpy.h>

#include "zipf.h"


#define PAYLOAD "rtqeijsuiggqlxkuuvsoerdzvgbphgpyrkecwbsynngpruiubtgzcwtfltjmmnpwapcbyioboiqdbxebcrqyehebezbdwyrvdbhxfsbearajfmmscsinujutdqcftxchgzptyfypojbmnpjovoartkwupbfowfvxhfimrltocjoousmumwrqvjxukjtztcyahxfoldflyquyixahobffjawyzzaawghbjgfhpajqevfflpgoiiotkqjbajhhvyhmnydmkxbrgpbavcdaanjopmnsewoebkqgqcbvxsblfgulcogxeqkaxnytevmpwobljlxtjhygawmbhktewhbiytjlmjxxtfmwytlogigvpfsgihyxgkmskppxikspqmnrarxbzihzwqufsltxiioyvcsrhjiqlcfqcavtxsrbqnogyxwerqlpiwextpuxvmflwbazjynzeiprcttniqtmdusjxwqfdzfocowaywwnmedqjfizajdqbetslgjqzsvadscxbdwrywffiwlgybupukobjpjlspaofjkmhszxzskirdieshmpfqsggjnhyiiadaumiisuontoomswlyhiyuwaupjwfjdeulymoqvmrwlncncqdaobnfmbiknxwadgbrpsfixqhnbbgnacuoivmlyxzflqnoobpqffnmkpxkzcyvoqnjvibphoxueqkjqhvgjurnxchlbncxvvbeettppluoxtzewefcaktcupcqueeymcyietauqtgycceyhfqpawsnydcuehnjfpkpnsvmpieauhoawchmpiymzirhvfxcnrtdsgmxoblqycfzfsgzvtdwdhcqtakezphfntfsxagkrtwofqsaipibbzqydlcvlungidzgqmkkreznoutrclipksaaefpokubbycpkpfddceydjxjmdpryujwqqpeohbmyhwrgeqtsirfykynkelarbjdfycjzjfuzcitiaadbfvwlwteefdsqvzarxhrtbsjeppkpszjrdfvkufynwvihygfykpvitpaqnxdedkytwlkcbttogfaqdsveqrtymflcxhvbumcifjklzpcqhfbpbwhyoaekjpcisswegubzcyjdwzyqxsshkpdsynfofowakfmasnudmzazgdxvvtajatwxskuitxjoewuvbhhhjdhqozoqfvquyioizhlqwqyaidvokcfbcsgnhfmthsmktbntvoleaeznatmmuawslyinilsvefdizgnbnayaxhzxxphuyzyfycmrsmidqlglknsxchkqstcsqaofvxgscfqsqlfvcwvbnhrxdclzxwettbkpsfyaafowvhlgmglesaehsionovcshwkxfxtaczcxpgzevwrbclqtkfhfbuztqhcylyufuhbcjodlxyssvvikfalxdxadvllbhyxelqsadeuxpjnochcxdlgxgjzderdhnwahmuzbqtwpnsuwhhxfwcbuahkgctljjqvqct"

static inline void payload_constant_num(char *payload, size_t payload_length, uint64_t num)
{
  *(uint64_t *)payload = num;
  rte_memcpy(payload + sizeof(uint64_t), PAYLOAD, payload_length - sizeof(uint64_t));
}

void payload_timestamp(char *payload, size_t payload_length)
{
  /* add timestamp. this timestamp is only valid on this machine */
  uint64_t timestamp = rte_get_timer_cycles();
  payload_constant_num(payload, payload_length, timestamp);
}

void payload_random_num(char *payload, size_t payload_length)
{
  static struct zipfgen *zgen = NULL;
  uint64_t num;

  if (zgen == NULL) {
    zgen = new_zipfgen(2000000, 0 /* zero means uniform*/);
    if (zgen == NULL) {
      fprintf(stderr, "Failed to initialize! @payload_random_num\n");
      return;
    }
  }
  // num = myrand() % 2000000;
  num = zgen->gen(zgen) - 1; 
  payload_constant_num(payload, payload_length, num);
}