/**
 * Framework for NoGo and similar games (C++ 11)
 * agent.h: Define the behavior of variants of the player
 *
 * Author: Theory of Computer Games (TCG 2021)
 *         Computer Games and Intelligence (CGI) Lab, NYCU, Taiwan
 *         https://cgilab.nctu.edu.tw/
 */

#pragma once
#include <string>
#include <random>
#include <sstream>
#include <map>
#include <type_traits>
#include <algorithm>
#include "board.h"
#include "action.h"
#include <fstream>
#include <math.h>
#include <map>
#include <chrono>

using hclock = std::chrono::high_resolution_clock;

enum PloyType
{
	randomPloy,
	mctsPloy
};

struct Node
{
	int nb;
	int value;
	int nb_rave;
	int value_rave;
	float h;
	std::vector<Node *> childNodes;
	action::place selectPlace;
};

#define FLT_MIN -10000000

class agent
{
public:
	agent(const std::string &args = "")
	{
		std::stringstream ss("name=unknown role=unknown " + args);
		for (std::string pair; ss >> pair;)
		{
			std::string key = pair.substr(0, pair.find('='));
			std::string value = pair.substr(pair.find('=') + 1);
			meta[key] = {value};
		}
	}
	virtual ~agent() {}
	virtual void open_episode(const std::string &flag = "") {}
	virtual void close_episode(const std::string &flag = "") {}
	virtual action take_action(const board &b) { return action(); }
	virtual bool check_for_win(const board &b) { return false; }

public:
	virtual std::string property(const std::string &key) const { return meta.at(key); }
	virtual void notify(const std::string &msg) { meta[msg.substr(0, msg.find('='))] = {msg.substr(msg.find('=') + 1)}; }
	virtual std::string name() const { return property("name"); }
	virtual std::string role() const { return property("role"); }

protected:
	typedef std::string key;
	struct value
	{
		std::string value;
		operator std::string() const { return value; }
		template <typename numeric, typename = typename std::enable_if<std::is_arithmetic<numeric>::value, numeric>::type>
		operator numeric() const { return numeric(std::stod(value)); }
	};
	std::map<key, value> meta;
};

/**
 * base agent for agents with randomness
 */
class random_agent : public agent
{
public:
	random_agent(const std::string &args = "") : agent(args)
	{
		if (meta.find("seed") != meta.end())
			engine.seed(int(meta["seed"]));
	}
	virtual ~random_agent() {}

protected:
	std::default_random_engine engine;
};

/**
 * random player for both side
 * put a legal piece randomly
 */
class player : public random_agent
{
public:
	player(const std::string &args = "") : random_agent("name=random role=unknown " + args),
										   space(board::size_x * board::size_y),
										   who(board::empty)
										   
	{
		if (name().find_first_of("[]():; ") != std::string::npos)
			throw std::invalid_argument("invalid name: " + name());
		if (role() == "black")
			who = board::black;
		if (role() == "white")
			who = board::white;
		if (who == board::empty)
			throw std::invalid_argument("invalid role: " + role());
		for (size_t i = 0; i < space.size(); i++)
		{
			space[i] = action::place(i, who);
			placeMap.insert(std::pair<action::place,std::vector<Node *> >(action::place(i, board::black), std::vector<Node *>()));
			placeMap.insert(std::pair<action::place,std::vector<Node *> >(action::place(i, board::white), std::vector<Node *>()));
		}
	}

	virtual action take_action(const board &state)
	{
		switch (ploy())
		{
		case PloyType::randomPloy:
			return random_action(state);
			break;
		case PloyType::mctsPloy:
			return mcts_action(state);
			break;
		default:
			break;
		}
		return action();
	}
	virtual void open_episode(const std::string &flag = "") {
		root = new Node{0, 0, 0, 0, 0, {}, action::place()};
		std::cout << "start Game" << std::endl;
	}

	virtual void close_episode(const std::string &flag = "") {
		deleteTree();
	}

private:
	std::vector<action::place> space;
	board::piece_type who;
	Node *root;
	std::map <action::place, std::vector<Node *> > placeMap; 
	PloyType ploy() const
	{
		if (property("ploy") == "mcts")
			return PloyType::mctsPloy;
		else
			return PloyType::randomPloy;
	}
	

	int timeLimit() const { return std::stoi(property("T")); }
	int testFlag() const { return std::stoi(property("Test")); }

	action random_action(const board &state)
	{
		std::shuffle(space.begin(), space.end(), engine);
		for (const action::place &move : space)
		{
			board after = state;
			if (move.apply(after) == board::legal)
			{
				return move;
			}
		}
		return action();
	}

	Node * checkIsExist(const board &state, Node *node)
	{
		for (int i = 0; i < node->childNodes.size(); i++)
		{
			action::place move = node->childNodes[i]->selectPlace;
			if (state.check_is_who(move.position().x,move.position().y) == move.color() &&
			    state.check_is_who(node->selectPlace.position().x, node->selectPlace.position().y) == node->selectPlace.color())
			{
				return node->childNodes[i];
			}
		}
		deleteTree();
		return new Node{0, 0, 0, 0, 0, {}, action::place()};
	}

	action::place compareBoard(const board &state)
	{
		return action();
	}

	action mcts_action(const board &state)
	{
		root = checkIsExist(state, root);
		create_node_leaf(state, who, root);
		int times_count = 0;
		int simulation_count = 10000;
		hclock::time_point start_time = hclock::now();
		hclock::time_point end_time = start_time;
		// std::cout << "===timeLimit===" << std::chrono::milliseconds(timeLimit()).count() << std::endl;
		do
		{
			playOneSequence(state, root);
			times_count++;
			end_time = hclock::now();
			
		} while(times_count < simulation_count && std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count() < std::chrono::milliseconds(timeLimit()).count());
		int maxIndex = -1;
		int maxnb = 0;
		for (int i = 0; i < root->childNodes.size(); i++)
		{
			if (root->childNodes[i]->nb > maxnb)
			{
				maxnb = root->childNodes[i]->nb;
				maxIndex = i;
			}
		}
		if (maxIndex != -1)
		{
			root = root->childNodes[maxIndex];
			return root->selectPlace;
		}		
		return action();
	}

	// One Simulation
	void playOneSequence(const board &state, Node *node)
	{
		std::vector<Node *> nodePath;
		nodePath.emplace_back(node);
		int index = 0;
		board after = state;
		board::piece_type currentWho = who;
		while (nodePath.back()->childNodes.size() != 0)
		{
			nodePath.emplace_back(descendByUCB1(after, nodePath.back()));
			nodePath.back()->selectPlace.apply(after);
			index++;
			if (currentWho == board::black)
				currentWho = board::white;
			else
				currentWho = board::black;
		}
		play_game_by_policy(after, currentWho, nodePath.back());
		create_node_leaf(after, currentWho, nodePath.back());
		updateValue(nodePath, nodePath.back()->value);
		updateVlaueRAVE(nodePath, nodePath.back()->value);
	}
	// Selection
	Node *descendByUCB1(const board &state, Node *node)
	{
		float nb = 0;
		for (Node *currentNode : node->childNodes)
		{
			nb += currentNode->nb;
		}

		std::vector<float> QList = std::vector<float>(node->childNodes.size());
		int max_index = 0;
		float max_Q = FLT_MIN;
		int min_index = 0;
		float min_Q = 1000000;
		for (int i = 0; i < node->childNodes.size(); i++)
		{
			float Q = 0;
			float Q_rave = 0;
			float exploration = 0;
			float beta = 0.5;
			
			if (node->childNodes[i]->nb == 0)
			{
				return node->childNodes[i];
			}
			else
			{
				Q = ((float)node->childNodes[i]->value / (float)node->childNodes[i]->nb);
				exploration = sqrt(2 * log(nb) / node->childNodes[i]->nb);
			}
			if (node->childNodes[i]->nb_rave > 0)
			{
				Q_rave = ((float)node->childNodes[i]->value_rave / (float)node->childNodes[i]->nb_rave);
			}
			float Q_star = Q * (1.0 - beta) + (Q_rave * beta) + exploration + node->childNodes[i]->h;
			if (Q_star > max_Q){
				max_Q = Q_star;
				max_index = i;
			}
			if (Q_star < min_Q){
				min_Q = Q_star;
				min_index = i;
			}
		}
		if (node->childNodes[0]->selectPlace.color() == who)
		{
			return node->childNodes[max_index];	
		}
		else 
		{
			return node->childNodes[min_index];
		}
		return node->childNodes[max_index];
	}

	// back propagation
	void updateValue(std::vector<Node *> nodePath, float v)
	{
		float value = v;
		for (int i = 0; i < nodePath.size() - 1; i++)
		{
			nodePath[i]->nb += 1;
			nodePath[i]->value += value;
			//value = -value; 
		}
	}

	void updateVlaueRAVE(std::vector<Node *> nodePath, float v)
	{
		if (nodePath.size() < 2) return;
		for (int i = 1; i < nodePath.size(); i++)
		{
			std::vector<Node *> nodes = placeMap[nodePath[i]->selectPlace];
			for (int j = 0; j < nodes.size(); j++)
			{
				nodes[j]->nb_rave +=1;
				nodes[j]->value_rave += v;
			}
		}
	}

	//
	void create_node_leaf(const board &state, board::piece_type whoRound, Node *node)
	{
		if (node->childNodes.size() > 0) return;
		std::vector<action::place> spaceRound; //(board::size_x * board::size_y);
		create_space(state, whoRound, spaceRound);
		for (const action::place &move : spaceRound)
		{
			board after = state;
			if (move.apply(after) == board::legal)
			{
				if (testFlag() == 1)
				{
					float liberty = get_liberty(state, move.position().x, move.position().y);
					node->childNodes.emplace_back(new Node{0, 0, 0, 0, liberty / (float)8.0, {}, move});
				}
				else if (testFlag() == 2)
				{
					node->childNodes.emplace_back(new Node{0, 0, 10, 20, 0, {}, move});
					placeMap[move].emplace_back(node->childNodes.back());
				}
				else if (testFlag() == 3)
				{
					float liberty = get_liberty(state, move.position().x, move.position().y);
					node->childNodes.emplace_back(new Node{0, 0, 10, 20, liberty / (float)8.0, {}, move});
					placeMap[move].emplace_back(node->childNodes.back());
				}
				else
				{
					node->childNodes.emplace_back(new Node{0, 0, 0, 0, 0, {}, move});
					placeMap[move].emplace_back(node->childNodes.back());
				}
			}
		}
	}

	//
	void play_game_by_policy(const board &state, board::piece_type whoFirst, Node *node)
	{
		board::piece_type whoWin = play(state, whoFirst);
		if (whoWin == who)
			node->value = 1;
		else
			node->value = 0;
		node->nb = 1;
	}

	board::piece_type play(const board &state, board::piece_type whoFirst)
	{
		board::piece_type whoRound = whoFirst;
		board after = state;
		action::place move;

		std::vector<action::place> spaceA, spaceB;
		board::piece_type p1 = whoFirst;
		board::piece_type p2 = board::black;
		if (p1 == board::black)
		{
			p2 = board::white;
		}
		create_space(state, p1, spaceA);
		create_space(state, p2, spaceB);
		std::reverse(spaceA.begin(), spaceA.end());
		std::reverse(spaceB.begin(), spaceB.end());
		do
		{
			while(!spaceA.empty())
			{
				action::place move = spaceA.back();
				if (move.apply(after) == board::legal)
				{
					spaceA.pop_back();
					break;
				}
				spaceA.pop_back();
			}
			while(!spaceB.empty())
			{
				action::place move = spaceB.back();
				if (move.apply(after) == board::legal)
				{
					spaceB.pop_back();
					break;
				}
				spaceB.pop_back();
			}
		} while (!spaceA.empty() && !spaceB.empty());
		if (spaceA.empty())
			whoRound = p2;
		else
			whoRound = p1;
		return whoRound;
	}

	void create_space(const board &state, board::piece_type whoFirst, std::vector<action::place> &spaceSort)
	{
		std::vector<action::place> best;
		std::vector<action::place> normal;
		std::vector<action::place> bad;
		for (int x = 0; x < board::size_x; x++)
		{
			for (int y = 0; y < board::size_y; y++)
			{
				int liberty = get_liberty(state, x, y);
				if (liberty == 4)
					spaceSort.emplace_back(action::place(x, y, whoFirst));
				else if (liberty == 3)
					best.emplace_back(action::place(x, y, whoFirst));
				else if (liberty == 2)
					normal.emplace_back(action::place(x, y, whoFirst));
				else
					bad.emplace_back(action::place(x, y, whoFirst));
			}
		}
		std::shuffle(best.begin(), best.end(), engine);
		std::shuffle(normal.begin(), normal.end(), engine);
		std::shuffle(bad.begin(), bad.end(), engine);
		spaceSort.insert(spaceSort.end(), best.begin(), best.end());
		spaceSort.insert(spaceSort.end(), normal.begin(), normal.end());
		spaceSort.insert(spaceSort.end(), bad.begin(), bad.end());
	}

	int get_liberty(const board &state, int x, int y)
	{
		int liberty = 0;
		board after = state;
		if (x < board::size_x - 1 && after[x + 1][y] == board::piece_type::empty)
			liberty++;
		if (x > 0 && after[x - 1][y] == board::piece_type::empty)
			liberty++;
		if (y < board::size_y - 1 && after[x][y + 1] == board::piece_type::empty)
			liberty++;
		if (y > 0 && after[x][y - 1] == board::piece_type::empty)
			liberty++;
		return liberty;
	}

	void deleteTree()
	{
		std::map <action::place, std::vector<Node *> >::iterator iter;
		for(auto &iter: placeMap)
		{
			for (auto &node: iter.second)
			{
				delete node;	
			}
		} 
		placeMap.clear();
	}
};