#include <alpm.h>
#include <bits/stdc++.h>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/strong_components.hpp>
#include <boost/graph/topological_sort.hpp>
#include <boost/program_options.hpp>

namespace po = boost::program_options;

typedef boost::adjacency_list<boost::setS, boost::vecS, boost::directedS>
    SetGraph;

template <typename T>
std::vector<T> to_vector(alpm_list_t *list) {
	std::vector<T> vec;
	for (auto it = list; it; it = it->next) {
		vec.push_back(static_cast<T>(it->data));
	}
	return vec;
}

int main(int argc, const char **argv) {
	po::options_description desc("Allowed options");
	// clang-format off
	desc.add_options()
		("help", "show help")
		("noopt", "break optdepends")
		("make", "don't break makedepends")
		("check", "don't break checkdepends")
		("all", "don't break any type of depends");
	// clang-format on

	po::variables_map vm;
	try {
		po::store(po::parse_command_line(argc, argv, desc), vm);
	} catch (const po::error &e) {
		std::cerr << e.what() << "\n";
		std::cerr << desc;
		return 1;
	}
	po::notify(vm);

	if (vm.count("help")) {
		std::cout << desc;
		return 0;
	}

	std::vector<std::add_pointer_t<decltype(alpm_pkg_get_depends)>>
	    depend_functions = {alpm_pkg_get_depends};

	if (!vm.count("noopt")) depend_functions.push_back(alpm_pkg_get_optdepends);
	if (vm.count("make") || vm.count("all")) {
		std::cerr << "WARNING: --make/--all option seems broken.\n"
		          << "Don't be surprised of any crashes!\n";
		depend_functions.push_back(alpm_pkg_get_makedepends);
	}
	if (vm.count("check") || vm.count("all")) {
		std::cerr << "WARNING: --check/--all option seems broken.\n"
		          << "Don't be surprised of any crashes!\n";
		depend_functions.push_back(alpm_pkg_get_checkdepends);
	}

	alpm_errno_t err;
	auto handle = alpm_initialize("/", "/var/lib/pacman", &err);
	if (!handle) {
		std::cerr << "Got error initializaing alpm: " << alpm_strerror(err)
		          << "\n";
		return 1;
	}

	auto db = alpm_get_localdb(handle);
	auto packages = to_vector<alpm_pkg_t *>(alpm_db_get_pkgcache(db));

	// Graph with edges (u -> v),
	// meaning that u depends on v
	SetGraph dep_graph(packages.size());

	std::map<std::string, std::vector<size_t>> provides_map;

	for (size_t i = 0; i < packages.size(); i++) {
		auto pkg = packages[i];
		auto name = alpm_pkg_get_name(packages[i]);
		provides_map[name].push_back(i);

		auto provides = to_vector<alpm_depend_t *>(alpm_pkg_get_provides(pkg));
		for (auto provide : provides) {
			provides_map[provide->name].push_back(i);
		}
	}

	for (size_t i = 0; i < packages.size(); i++) {
		auto pkg = packages[i];
		auto name = alpm_pkg_get_name(packages[i]);

		for (auto dep_fun : depend_functions) {
			auto depends = to_vector<alpm_depend_t *>(dep_fun(pkg));
			for (auto dep : depends) {
				auto provides_is = provides_map[dep->name];
				if (dep_fun == alpm_pkg_get_depends &&
				    provides_is.size() == 0) {
					std::cerr << "WARNING: No package provides " << dep->name
					          << ", required by " << name << "\n";
				}

				// TODO: How should we handle multiple provides?
				for (auto j : provides_is) {
					boost::add_edge(i, j, dep_graph);
				}
			}
		}
	}

	std::vector<size_t> component_map(packages.size());
	size_t num_components =
	    boost::strong_components(dep_graph, &component_map[0]);

	std::vector<std::vector<size_t>> components(num_components);
	// Graph with edges (c1 -> c2),
	// meaning that all of c2 depends on c1
	SetGraph component_graph(num_components);
	for (size_t i = 0; i < packages.size(); i++) {
		size_t c1 = component_map[i];
		components[c1].push_back(i);
		for (auto [e, e_end] = out_edges(i, dep_graph); e != e_end; e++) {
			size_t c2 = component_map[target(*e, dep_graph)];
			if (c1 != c2) {
				boost::add_edge(c1, c2, component_graph);
			}
		}
	}

	auto should_remove = [&](size_t c) {
		for (size_t j : components[c]) {
			auto reason = alpm_pkg_get_reason(packages[j]);
			if (reason == ALPM_PKG_REASON_EXPLICIT) {
				return false;
			}
		}
		return true;
	};

	std::vector<size_t> indegree(num_components);
	std::vector<size_t> stack;
	for (auto [e, e_end] = edges(component_graph); e != e_end; e++) {
		indegree[target(*e, component_graph)]++;
	}

	for (size_t i = 0; i < num_components; i++) {
		if (indegree[i] == 0) stack.push_back(i);
	}

	std::vector<std::string> unused_deps;
	while (!stack.empty()) {
		size_t i = stack.back();
		stack.pop_back();

		if (!should_remove(i)) continue;

		for (int j : components[i]) {
			unused_deps.push_back(alpm_pkg_get_name(packages[j]));
		}

		for (auto [e, e_end] = out_edges(i, component_graph); e != e_end; e++) {
			size_t j = target(*e, component_graph);
			if (--indegree[j] == 0) {
				stack.push_back(j);
			}
		}
	}

	std::sort(unused_deps.begin(), unused_deps.end());
	for (auto dep : unused_deps) {
		std::cout << dep << "\n";
	}

	alpm_release(handle);

	if (unused_deps.empty()) {
		std::cerr << "No packages should be removed.\n";
		return 1;
	}

	if (!isatty(STDOUT_FILENO)) return 0;

	std::cerr << "Remove with pacman? [yN] ";
	char ans = getchar();
	if (ans != 'y' && ans != 'Y') return 0;

	auto cmd_args = new const char *[unused_deps.size() + 4];
	cmd_args[0] = "sudo";
	cmd_args[1] = "pacman";
	cmd_args[2] = "-R";
	size_t i = 3;
	for (auto &dep : unused_deps) {
		cmd_args[i++] = dep.c_str();
	}
	cmd_args[i] = nullptr;

	execv("/usr/bin/sudo", const_cast<char *const *>(cmd_args));
}
