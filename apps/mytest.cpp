import std;
import graaf;

int main()
{
    graaf::directed_graph<const char, int> g;
    std::unordered_map<std::string, graaf::vertex_id_t> id_map;

    id_map["a"] = g.add_vertex('a');
    id_map["b"] = g.add_vertex('b');
    id_map["c"] = g.add_vertex('c');
    id_map["d"] = g.add_vertex('d');

    g.add_edge(id_map["a"], id_map["b"], 1);
    g.add_edge(id_map["a"], id_map["c"], 1);
    g.add_edge(id_map["b"], id_map["d"], 1);
    g.add_edge(id_map["c"], id_map["d"], 1);

    std::unordered_set<graaf::vertex_id_t> visited;

    graaf::algorithm::breadth_first_traverse(g, id_map["a"], [&](graaf::edge_id_t e) {
        auto [src, dst] = e;
        if (dst != id_map["a"])
        {
            std::cout << g.get_vertex(dst) << "\n";
        }
    });
}
