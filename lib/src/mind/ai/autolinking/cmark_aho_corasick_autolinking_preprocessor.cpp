/*
 cmark_aho_corasick_autolinking_preprocessor.cpp     MindForger thinking notebook

 Copyright (C) 2016-2019 Martin Dvorak <martin.dvorak@mindforger.com>

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
#include "cmark_aho_corasick_autolinking_preprocessor.h"

/*
 * Plan:
 *
 * - ensure correctness FIRST ~ unit tests:
 *    - no trailing spaces
 *    - protection of bullet lists
 *    - protection of links/images/...
 *    - protection of inlined MATH $..$
 *    - blacklist ~ don't autolink e.g. http (to protect cmark's URLs autolinking)
 *
 * - polish correct version
 *    - code to methods
 *
 * - performance
 *    - avoid autolinking whole O on its load - it's not needed
 *    - map search structure instead of Aho
 *    - benchmark on C
 *    - configurable time limit on autolinking and leave on exceeding it
 */

namespace m8r {

using namespace std;

CmarkAhoCorasickAutolinkingPreprocessor::CmarkAhoCorasickAutolinkingPreprocessor(Mind& mind)
    : AutolinkingPreprocessor{mind}
{
}

CmarkAhoCorasickAutolinkingPreprocessor::~CmarkAhoCorasickAutolinkingPreprocessor()
{
}

/**
 * @brief Inject links into MD represented as a list of strings.
 */
void CmarkAhoCorasickAutolinkingPreprocessor::process(const std::vector<std::string*>& md, std::vector<std::string*>& amd)
{
#ifdef MF_MD_2_HTML_CMARK

#ifdef DO_MF_DEBUG
    MF_DEBUG("[Autolinking] begin CMARK-AHO" << endl);
    string ds{};
    toString(md, ds);
    MF_DEBUG("[Autolinking] input:" << endl << ">>" << ds << "<<" << endl);

    auto begin = chrono::high_resolution_clock::now();
#endif

    insensitive = Configuration::getInstance().isAutolinkingCaseInsensitive();
    updateTrieIndex();

    if(md.size()) {

        // IMPROVE measure time in here and if over give limit, than STOP injecting
        // and leave i.e. what happens is that a time SLA will be fulfilled and
        // some part (prefix) of the input MD will be autolinked.

        bool inCodeBlock=false, inMathBlock=false;
        for(string* l:md) {
            // every line is autolinked SEPARATELY
            string* nl = new string{};

            // skip code/math/... blocks
            if(stringStartsWith(*l, CODE_BLOCK)) {
                inCodeBlock = !inCodeBlock;

                nl->assign(*l);
                amd.push_back(nl);
            } else if(stringStartsWith(*l, MATH_BLOCK)) {
                inMathBlock= !inMathBlock;

                nl->assign(*l);
                amd.push_back(nl);
            } else if(l) {
                if(l->size()) {
                    parseMarkdownLine(l, nl);
                    amd.push_back(nl);
                } else {
                    nl->assign(*l);
                    amd.push_back(nl);
                }
            } else {
                delete nl;
                amd.push_back(nullptr);
            }
        }
    }
#ifdef DO_MF_DEBUG
    ds.clear();
    toString(amd, ds);
    MF_DEBUG("[Autolinking] output:" << endl << ">>" << ds << "<<" << endl);

    auto end = chrono::high_resolution_clock::now();
    MF_DEBUG("[Autolinking] MD autolinked in: " << chrono::duration_cast<chrono::microseconds>(end-begin).count()/1000000.0 << "ms" << endl);
#endif

#else
    amd = md;
#endif
}

void CmarkAhoCorasickAutolinkingPreprocessor::parseMarkdownLine(const std::string* md, std::string* amd)
{
#ifdef DO_MF_DEBUG
    MF_DEBUG("[Autolinking] parsing line:" << endl << ">>" << *md << "<<" << endl);
#endif

#ifdef MF_MD_2_HTML_CMARK
    const char* smd = md->c_str();

    // cmark identifies '    * my bullet' as code block, which is wrong > workaround
    string attic{};
    if(stringStartsWith(smd, "    ")) {
        attic.assign(*md);
        attic[0] = '@';
        smd = attic.c_str();
        MF_DEBUG("[Autolinking] avoiding CODE block interpretation:" << endl << ">>" << attic << "<<" << endl);
    }

    cmark_node* document = cmark_parse_document(
        smd,
        strlen(smd),
        CMARK_OPT_DEFAULT);

    // AST iteration
    cmark_iter *i = cmark_iter_new(document);
    cmark_event_type eventType;
    cmark_node* zombieNode{};

    bool inLinkImgOrCode = false;

    while ((eventType = cmark_iter_next(i)) != CMARK_EVENT_DONE) {
        cmark_node *node = cmark_iter_get_node(i);

        if(zombieNode) {
            cmark_node_unlink(zombieNode);
            cmark_node_free(zombieNode);
            zombieNode = nullptr;
        }

        // IMPROVE make this a debug method
        // do something with `cur` and `ev_type`
        switch(eventType) {
        case CMARK_EVENT_ENTER:
            MF_DEBUG("ENTER");
            break;
        case CMARK_EVENT_EXIT:
            MF_DEBUG("LEAVE");
            break;
        case CMARK_EVENT_DONE:
            MF_DEBUG("DONE");
            break;
        case CMARK_EVENT_NONE:
            MF_DEBUG("NONE");
            break;
        default:
            MF_DEBUG(".");
        }

        // Nodes must only be modified after an `EXIT` event,
        // or an `ENTER` event for leaf nodes.

        // inlined autolinking constructions to avoid - auto-link in:
        // - link
        // - image
        // - code
        // ... iterate nodes and skip <text/> if UNDER link/image/code,
        // otherwise trim text and try to autolink it.

        switch(cmark_node_get_type(node)) {
        case CMARK_NODE_CODE:
            MF_DEBUG(" code");
        case CMARK_NODE_LINK:
            MF_DEBUG(" link");
        case CMARK_NODE_IMAGE:
            MF_DEBUG(" image");
            if(eventType == CMARK_EVENT_ENTER) {
                inLinkImgOrCode = true;
            } else if (eventType == CMARK_EVENT_EXIT) {
                inLinkImgOrCode = false;
            }
            break;
        case CMARK_NODE_TEXT:
            MF_DEBUG(" text '" << cmark_node_get_literal(node) << "'" << endl);
            if(!inLinkImgOrCode) {
                // replace text node w/ sequence of text and link nodes
                injectThingsLinks(node);
                zombieNode = node;
            }
            break;
        default:
            MF_DEBUG(" .");
        }

        MF_DEBUG(endl);
    }

    cmark_iter_free(i);

    char* cmm = cmark_render_commonmark(document, 0, 0);
    amd->assign(cmm);
    amd->pop_back();

    if(attic.size()) {
        (*amd)[4] = ' ';
        amd->erase(0, 1);
    }

#ifdef DO_MF_DEBUG
    char* xml = cmark_render_xml(document, 0);
    MF_DEBUG("[Autolinking] Line's cmark AST as XML:" << endl << endl);
    MF_DEBUG(xml << endl);
    free(xml);

    MF_DEBUG("[Autolinking] Line's cmark AST as MD:" << endl << ">>" << *amd << "<<" << endl);
#endif

    free(cmm);
    cmark_node_free(document);

#else
    amd->assign(*md);
#endif
}

void CmarkAhoCorasickAutolinkingPreprocessor::injectThingsLinks(cmark_node* origNode)
{
    // copy w to t as it will be chopped word/match by word/match from head to tail
    string txt{cmark_node_get_literal(origNode)};
    string pre{}, at{}, chop{};
    size_t preSize{};

    cmark_node* node{};
    cmark_node* linkNode{};
    cmark_node* txtNode{};

    // allowed trailing chars (\\... added newly)
    string tMatch{" \t,:;.!?<>{}&()-+/*\\_=%~#$^[]'\""};

#ifdef DO_MF_DEBUG
    MF_DEBUG("[Autolinking] Injecting links to: '" << txt << "'" << endl);
#endif

    while(txt.size()>0) {
        // skip trailing chars and append them
        preSize = 0;
        while(preSize < txt.size()) {
            if(tMatch.find(txt.at(preSize)) != string::npos) {
                preSize++;
            } else {
                break;
            }
        }
        if(preSize) {
            // chop trailing chars prefix from input and append it to result
            at.append(txt.substr(0, preSize));
            txt = txt.substr(preSize);

            MF_DEBUG("  Skipping trailing chars: '" << txt.substr(0, preSize) << "'" << endl);
            MF_DEBUG("     txt: '" << txt << "'" << endl);
            MF_DEBUG("     at : '" << at  << "'" << endl);
        }

        // try to match word
        pre.clear();
        MF_DEBUG("  Trie search txt: '" << txt << "'" << endl);
        if(trie->findLongestPrefixWord(txt, pre)) {
            MF_DEBUG("    Matched prefix: '" << pre << "'" << endl);

            // avoid word PREFIX matches ~ ensure that WHOLE world is matched:
            // - match followed by trailing char
            // - match until EOL

            // determine trailing char
            char tChar{txt.size()==pre.size()?' ':txt.at(pre.size())};
            MF_DEBUG("    Match's trailing char: '" << tChar << "'" << endl);
            if(tMatch.find(tChar) != string::npos
                 ||
               txt.size() == pre.size()) {

                // AST: add text node w/ content preceding link
                // IMPROVE make this method
                if(at.size()) {
                    txtNode = cmark_node_new(CMARK_NODE_TEXT);
                    cmark_node_set_literal(txtNode, at.c_str());
                    // IMPROVE make this private method
                    if(node) {
                        cmark_node_insert_after(node, txtNode);
                    } else {
                        cmark_node_insert_before(origNode, txtNode);
                    }
                    node = txtNode;

                    at.clear();
                }

                // AST: add link
                // IMPROVE make this method
                linkNode = cmark_node_new(CMARK_NODE_LINK);
                string link{MF_URL_PROTOCOL};
                link.append(pre);
                cmark_node_set_url(linkNode, link.c_str());
                txtNode = cmark_node_new(CMARK_NODE_TEXT);
                cmark_node_set_literal(txtNode, pre.c_str());
                cmark_node_append_child(linkNode, txtNode);
                if(node) {
                    cmark_node_insert_after(node, linkNode);
                } else {
                    cmark_node_insert_before(origNode, linkNode);
                }
                node = linkNode;

                // chop linked prefix word from input word w
                if(txt.size() == pre.size()) {
                    txt.clear();
                } else {
                    txt = txt.substr(pre.size());
                }
                // trailing char will be handled later
                at.clear();

                MF_DEBUG("    txt: '" << txt << "'" << endl);
                MF_DEBUG("    at : '" << at  << "'" << endl);
            } else {
                // invalid trailing char > matched a NOT-whole-world prefix > skip and append one word

                // TODO make it method
                // current w prefix was NOT linked > chop it and append it
                size_t begin = txt.find_first_of(" \t");
                if(begin != string::npos) {
                    chop = txt.substr(0, begin);
                    txt = txt.substr(begin+1);
                    at.append(chop);
                    // TODO IMPORTANT append what was found - space or tab! simply index there
                    at.append(" ");

                    MF_DEBUG("  Skiping word: '" << chop << "'" << endl);
                    MF_DEBUG("     txt: '" << txt << "'" << endl);
                    MF_DEBUG("     at : '" << at  << "'" << endl);
                } else {
                    // no more words (prefix already checked) > DONE
                    at.append(txt);

                    MF_DEBUG("  DONE no-more words: '" << chop << "'" << endl);
                    MF_DEBUG("     txt: '" << txt << "'" << endl);
                    MF_DEBUG("     at : '" << at  << "'" << endl);

                    break;
                }
            }
        } else {
            // didn't match prefix > skip and append one word

            // TODO make it method
            // current w prefix was NOT linked > chop it and append it
            size_t begin = txt.find_first_of(" \t");
            if(begin != string::npos) {
                chop = txt.substr(0, begin);
                txt = txt.substr(begin+1);
                at.append(chop);
                // TODO IMPORTANT append what was found - space or tab! simply index there
                at.append(" ");

                MF_DEBUG("  Skiping word: '" << chop << "'" << endl);
                MF_DEBUG("     txt: '" << txt << "'" << endl);
                MF_DEBUG("     at : '" << at  << "'" << endl);
            } else {
                // no more words (prefix already checked) > DONE
                at.append(txt);

                MF_DEBUG("  DONE no-more words: '" << chop << "'" << endl);
                MF_DEBUG("     txt: '" << txt << "'" << endl);
                MF_DEBUG("     at : '" << at  << "'" << endl);

                break;
            }
        }
    }

    // AST: add text node w/ content preceding link
    // IMPROVE make this method
        if(at.size()) {
            txtNode = cmark_node_new(CMARK_NODE_TEXT);
            cmark_node_set_literal(txtNode, at.c_str());
            // IMPROVE make this private method
            if(node) {
                cmark_node_insert_after(node, txtNode);
            } else {
                cmark_node_insert_before(origNode, txtNode);
            }
            node = txtNode;

            // IMPROVE: can be commented
            at.clear();
        }
    }

} // m8r namespace
