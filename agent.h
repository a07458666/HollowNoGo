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

enum PloyType
{
	randomPloy,
	mctsPloy
};

struct Node
{
	int nb;
	int value;
	std::vector<Node *> childNodes;
	action::place selectPlace;
};

#define FLT_MIN 1.175494351e-38

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
										   space(board::size_x * board::size_y), who(board::empty)
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
			space[i] = action::place(i, who);
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

private:
	std::vector<action::place> space;
	board::piece_type who;
	PloyType ploy() const
	{
		if (property("ploy") == "mcts")
			return PloyType::mctsPloy;
		else
			return PloyType::randomPloy;
	}

	action random_action(const board &state)
	{
		// create_space(state, who, space);
		// std::cout << who << std::endl;
		std::shuffle(space.begin(), space.end(), engine);
		for (const action::place &move : space)
		{
			board after = state;
			if (move.apply(after) == board::legal)
				return move;
		}
		return action();
	}

	action mcts_action(const board &state)
	{
		Node root = {0, 0, {}, action::place()};
		create_node_leaf(state, who, &root);
		int times_count = 0;
		while (times_count < 500)
		{
			playOneSequence(state, &root);
			times_count++;
		}
		int maxIndex = -1;
		int maxnb = 0;
		// std::cout << "===========START===========" << std::endl;
		// std::cout << state << std::endl;
		for (int i = 0; i < root.childNodes.size(); i++)
		{
			if (root.childNodes[i]->nb > maxnb)
			{
				// board after = state;
				// root.childNodes[i]->selectPlace.apply(after);
				// std::cout << "nb " << root.childNodes[i]->nb << std::endl;
				// std::cout << "(x,y)" << root.childNodes[i]->selectPlace.position().x << "," << root.childNodes[i]->selectPlace.position().y << std::endl;
				// std::cout << after << std::endl;
				maxnb = root.childNodes[i]->nb;
				maxIndex = i;
			}
		}
		if (maxIndex != -1)
			return root.childNodes[maxIndex]->selectPlace;
		return action();
	}

	// One Simulation
	void playOneSequence(const board &state, Node *rootNode)
	{
		std::vector<Node *> nodePath;
		nodePath.emplace_back(rootNode);
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
		updateValue(nodePath, -nodePath.back()->value);
	}
	// Selection
	Node *descendByUCB1(const board &state, Node *node)
	{
		float nb = 0;
		for (Node *currentNode : node->childNodes)
		{
			nb += currentNode->nb;
		}

		float max_v = FLT_MIN;
		float max_index = 0;
		for (int i = 0; i < node->childNodes.size(); i++)
		{
			if (node->childNodes[i]->nb == 0)
			{
				max_v = FLT_MIN;
				max_index = i;
				break;
			}
			else
			{
				float v = (-node->childNodes[i]->value / node->childNodes[i]->nb) + (sqrt(2 * log(nb) / node->childNodes[i]->nb));
				if (v > max_v)
				{
					max_v = v;
					max_index = i;
				}
			}
		}
		// std::cout << "max_index " << max_index << std::endl;
		// std::cout << "node->childNodes.size() " << node->childNodes.size() << std::endl;
		return node->childNodes[max_index];
	}

	// back propagation
	void updateValue(std::vector<Node *> nodePath, float v)
	{
		float locvalue = v;
		for (int i = 0; i < nodePath.size() - 1; i++)
		{
			nodePath[i]->nb += 1;
			nodePath[i]->value += locvalue;
			locvalue = -locvalue;
		}
	}

	//
	void create_node_leaf(const board &state, board::piece_type whoRound, Node *node)
	{
		// std::vector<action::place> spaceRound(board::size_x * board::size_y);
		// for (size_t i = 0; i < spaceRound.size(); i++)
		// 	spaceRound[i] = action::place(i, whoRound);
		std::vector<action::place> spaceRound; //(board::size_x * board::size_y);
		create_space(state, whoRound, spaceRound);
		auto newSize = std::min((size_t)20, spaceRound.size());
		spaceRound.resize(newSize);
		// std::shuffle(spaceRound.begin(), spaceRound.end(), engine);
		// std::cout << "==========BEGIN===========" << std::endl;
		// std::cout << "whoRound" << whoRound << std::endl;
		// std::cout << state << std::endl;
		// std::cout << "==========ALL IN===========" << std::endl;
		for (const action::place &move : spaceRound)
		{
			board after = state;
			if (move.apply(after) == board::legal)
			{
				// std::cout << "(x,y)" << move.position().x << "," << move.position().y << std::endl;
				node->childNodes.emplace_back(new Node{0, 0, {}, move});
				// std::cout << after << std::endl;
			}
		}
		// std::cout << "==========END===========" << std::endl;
	}

	//
	void play_game_by_policy(const board &state, board::piece_type whoFirst, Node *node)
	{
		board::piece_type whoWin = play(state, whoFirst);
		if (whoWin == board::black)
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
		do
		{
			move = playOneHand(after, whoRound);
			move.apply(after);
			if (whoRound == board::black)
				whoRound = board::white;
			else
				whoRound = board::black;
		} while (move != -1u);
		return whoRound;
	}

	action playOneHand(const board &state, board::piece_type whoFirst)
	{
		std::vector<action::place> spaceRound;
		create_space(state, whoFirst, spaceRound);
		for (const action::place &move : spaceRound)
		{
			board after = state;
			if (move.apply(after) == board::legal)
			{
				// std::cout << after << std::endl;
				return move;
			}
		}
		return action();
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
				board after = state;
				if (action::place(x, y, whoFirst).apply(after) == board::legal)
				{
					int liberty = 0;
					if (x < board::size_x - 1 && after[x + 1][y] == board::piece_type::empty)
						liberty++;
					if (x > 0 && after[x - 1][y] == board::piece_type::empty)
						liberty++;
					if (y < board::size_y - 1 && after[x][y + 1] == board::piece_type::empty)
						liberty++;
					if (y > 0 && after[x][y - 1] == board::piece_type::empty)
						liberty++;
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
		}
		spaceSort.insert(spaceSort.end(), best.begin(), best.end());
		spaceSort.insert(spaceSort.end(), normal.begin(), normal.end());
		spaceSort.insert(spaceSort.end(), bad.begin(), bad.end());
	}
};
