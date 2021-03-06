/* This file is part of RTags (http://rtags.net).

   RTags is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   RTags is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with RTags.  If not, see <http://www.gnu.org/licenses/>. */

#include "ListSymbolsJob.h"

#include "Project.h"
#include "rct/Log.h"
#include "RTags.h"
#include "Server.h"

const Flags<QueryJob::JobFlag> defaultFlags = (QueryJob::WriteUnfiltered | QueryJob::QuietJob);
const Flags<QueryJob::JobFlag> elispFlags = (defaultFlags | QueryJob::QuoteOutput);

ListSymbolsJob::ListSymbolsJob(const std::shared_ptr<QueryMessage> &query, const std::shared_ptr<Project> &proj)
    : QueryJob(query, proj, query->flags() & QueryMessage::Elisp ? elispFlags : defaultFlags),
      string(query->query())
{
}

int ListSymbolsJob::execute()
{
    Set<String> out;
    std::shared_ptr<Project> proj = project();
    if (proj) {
        if (queryFlags() & QueryMessage::WildcardSymbolNames
            && (string.contains('*') || string.contains('?')) && !string.endsWith('*')) {
            string += '*';
        }
        List<QueryMessage::PathFilter> filters = pathFilters();
        List<Path> paths;
        for (const auto &filter : filters) {
            if (filter.mode == QueryMessage::PathFilter::Self) {
                paths.append(filter.pattern);
                if (!paths.last().isFile()) {
                    paths.clear();
                    break;
                }
            } else {
                paths.clear();
                break;
            }
        }
        if (!paths.isEmpty()) {
            out = listSymbolsWithPathFilter(proj, paths);
        } else {
            out = listSymbols(proj);
        }
    }

    if (queryFlags() & QueryMessage::Elisp) {
        write("(list", IgnoreMax | DontQuote);
        for (Set<String>::const_iterator it = out.begin(); it != out.end(); ++it) {
            write(*it);
        }
        write(")", IgnoreMax | DontQuote);
    } else {
        List<String> sorted = out.toList();
        if (queryFlags() & QueryMessage::ReverseSort) {
            std::sort(sorted.begin(), sorted.end(), std::greater<String>());
        } else {
            std::sort(sorted.begin(), sorted.end());
        }
        const int count = sorted.size();
        for (int i = 0; i < count; ++i) {
            write(sorted.at(i));
        }
    }
    return out.isEmpty() ? 1 : 0;
}

Set<String> ListSymbolsJob::listSymbolsWithPathFilter(const std::shared_ptr<Project> &project, const List<Path> &paths) const
{
    Set<String> out;
    const bool imenu = queryFlags() & QueryMessage::IMenu;
    const bool wildcard = queryFlags() & QueryMessage::WildcardSymbolNames && (string.contains('*') || string.contains('?'));
    const bool stripParentheses = queryFlags() & QueryMessage::StripParentheses;
    const bool caseInsensitive = queryFlags() & QueryMessage::MatchCaseInsensitive;
    const String::CaseSensitivity cs = caseInsensitive ? String::CaseInsensitive : String::CaseSensitive;
    for (int i=0; i<paths.size(); ++i) {
        const Path file = paths.at(i);
        const uint32_t fileId = Location::fileId(file);
        if (!fileId)
            continue;
        auto symbols = project->openSymbols(fileId);
        if (!symbols)
            continue;
        const int count = symbols->count();
        for (int j=0; j<count; ++j) {
            const Symbol &symbol = symbols->valueAt(j);
            if (imenu && !isImenuSymbol(symbol))
                continue;
            const String &symbolName = symbol.symbolName;
            if (!string.isEmpty()) {
                if (wildcard) {
                    if (!Project::matchSymbolName(string, symbolName, cs)) {
                        continue;
                    }
                } else if (!symbolName.contains(string, cs)) {
                    continue;
                }
            }

            const int paren = string.indexOf('(');
            if (paren == -1) {
                out.insert(string);
            } else {
                if (!RTags::isFunctionVariable(string))
                    out.insert(string.left(paren));
                if (!stripParentheses)
                    out.insert(string);
            }
            out.insert(symbolName);
        }
    }
    return out;
}

Set<String> ListSymbolsJob::listSymbols(const std::shared_ptr<Project> &project) const
{
    const bool hasFilter = QueryJob::hasFilter();
    const bool stripParentheses = queryFlags() & QueryMessage::StripParentheses;
    const bool imenu = queryFlags() & QueryMessage::IMenu;

    Set<String> out;
    auto inserter = [this, &project, hasFilter, stripParentheses, imenu, &out](Project::SymbolMatchType,
                                                                               const String &string,
                                                                               const Set<Location> &locations) {
        if (hasFilter) {
            bool ok = false;
            for (const auto &l : locations) {
                if (filter(l.path())) {
                    ok = true;
                    break;
                }
            }
            if (!ok)
                return;
        }
        if (imenu) {
            const Symbol sym = project->findSymbol(*locations.begin());
            if (!isImenuSymbol(sym))
                return;
        }
        const int paren = string.indexOf('(');
        if (paren == -1) {
            out.insert(string);
        } else {
            if (!RTags::isFunctionVariable(string))
                out.insert(string.left(paren));
            if (!stripParentheses)
                out.insert(string);
        }
    };

    project->findSymbols(string, inserter, queryFlags());
    return out;
}
