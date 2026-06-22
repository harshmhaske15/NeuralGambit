import json
import copy  # use it for deepcopy if needed
import math  # for math.inf
import logging

logging.basicConfig(format='%(levelname)s - %(asctime)s - %(message)s', datefmt='%d-%b-%y %H:%M:%S',
                    level=logging.INFO)

# Global variables in which you need to store player strategies (this is data structure that'll be used for evaluation)
# Mapping from histories (str) to probability distribution over actions
strategy_dict_x = {}
strategy_dict_o = {}


class History:
    def __init__(self, history=None):
        """
        # self.history : Eg: [0, 4, 2, 5]
            keeps track of sequence of actions played since the beginning of the game.
            Each action is an integer between 0-8 representing the square in which the move will be played as shown
            below.
              ___ ___ ____
             |_0_|_1_|_2_|
             |_3_|_4_|_5_|
             |_6_|_7_|_8_|

        # self.board
            empty squares are represented using '0' and occupied squares are either 'x' or 'o'.
            Eg: ['x', '0', 'x', '0', 'o', 'o', '0', '0', '0']
            for board
              ___ ___ ____
             |_x_|___|_x_|
             |___|_o_|_o_|
             |___|___|___|

        # self.player: 'x' or 'o'
            Player whose turn it is at the current history/board

        :param history: list keeps track of sequence of actions played since the beginning of the game.
        """
        if history is not None:
            self.history = history
            self.board = self.get_board()
        else:
            self.history = []
            self.board = ['0', '0', '0', '0', '0', '0', '0', '0', '0']
        self.player = self.current_player()

    def current_player(self):
        """ Player function
        Get player whose turn it is at the current history/board
        :return: 'x' or 'o' or None
        """
        total_num_moves = len(self.history)
        if total_num_moves < 9:
            if total_num_moves % 2 == 0:
                return 'x'
            else:
                return 'o'
        else:
            return None

    def get_board(self):
        """ Play out the current self.history and get the board corresponding to the history in self.board.

        :return: list Eg: ['x', '0', 'x', '0', 'o', 'o', '0', '0', '0']
        """
        board = ['0', '0', '0', '0', '0', '0', '0', '0', '0']
        for i in range(len(self.history)):
            if i % 2 == 0:
                board[self.history[i]] = 'x'
            else:
                board[self.history[i]] = 'o'
        return board

    def is_win(self):
        win=[[0,1,2],[3,4,5],[6,7,8],[0,3,6],[1,4,7],[2,5,8],[0,4,8],[2,4,6]]
        board = self.get_board()
        Result='n'
        for i in range(0,7):
            if(board[win[i][0]]==board[win[i][1]] and board[win[i][0]]==board[win[i][2]]):
                Result=board[win[i][1]]
                break
        return Result

    def is_draw(self):
        win=[[0,1,2],[3,4,5],[6,7,8],[0,3,6],[1,4,7],[2,5,8],[0,4,8],[2,4,6]]
        board = self.get_board()
        Result=True
        for i in range(0,7):
            if(board[win[i][0]]==board[win[i][1]] and board[win[i][0]]==board[win[i][2]]):
                Result=False
                break
        return Result

    def get_valid_actions(self):
        return [i for i in range(9) if self.board[i] == '0']

    def is_terminal_history(self):
        isWin=self.is_win()
        if(isWin !='n'):
            return True
        else:
            isDraw=self.is_draw()
            if(isDraw):
                return True
        return False
        
    def get_utility_given_terminal_history(self):
        isWin=self.is_win()
        if(isWin =='x'):
            return 1
        elif(isWin =='o'):
            return -1
        else:
            return 0


    def update_history(self, action):
      new_history = self.history + [action]
      return History(new_history)


def backward_induction(history_obj):
    global strategy_dict_x, strategy_dict_o

    if history_obj.is_terminal_history():
        return history_obj.get_utility_given_terminal_history()

    player = history_obj.player
    valid_actions = history_obj.get_valid_actions()

    best_value = -math.inf if player == 'x' else math.inf
    best_action = None

    for action in valid_actions:
        child = history_obj.update_history(action)
        value = backward_induction(child)

        if player == 'x' and value > best_value:
            best_value = value
            best_action = action
        elif player == 'o' and value < best_value:
            best_value = value
            best_action = action

    key = ''.join(str(a) for a in history_obj.history)
    strategy = {str(a): 0 for a in range(9)}
    strategy[str(best_action)] = 1

    if player == 'x':
        strategy_dict_x[key] = strategy
    else:
        strategy_dict_o[key] = strategy

    return best_value


def solve_tictactoe():
    backward_induction(History())
    with open('./policy_x.json', 'w') as f:
        json.dump(strategy_dict_x, f)
    with open('./policy_o.json', 'w') as f:
        json.dump(strategy_dict_o, f)
    return strategy_dict_x, strategy_dict_o


if __name__ == "__main__":
    logging.info("Start")
    solve_tictactoe()
    logging.info("End")
