import os
import random
import time
# import tensorflow as tf
from collections import deque

import numpy as np
from keras.callbacks import TensorBoard
from keras.layers import (Activation, Conv2D, Dense, Dropout, Embedding,
                          Flatten, MaxPooling2D, Reshape)
# import keras.backend.tensorflow_backend as backend
from keras.models import Sequential, load_model

from tensorflow.keras.optimizers import Adam


from InterSliceSch import InterSliceScheduler
from Slice import *

LEARNING = True  # training mode for formal DQN v2
MODEL_NAME = "formal_dqn_v2"  # separate formal-verification model
RETRAIN = False  # start v2 training from scratch
SHOW_EVERY = 20  # how often to display the current episode number and average score during training

# Defining a new scheduler class that inherits from InterSliceScheduler
class vanillaDQN_Scheduler(InterSliceScheduler):
    def __init__(self, ba, fr, dm, tdd, gr):
        InterSliceScheduler.__init__(self, ba, fr, dm, tdd, gr)
        self.granularity = gr  # setting the granularity of the scheduler
        self.prev_urlcc_sent = 0
        self.prev_urlcc_lost = 0
        
        # Setting model hyper-parameters
        self.windows_size = 30  # number of time steps to consider in each slice
        self.learning_rate = 0.2  # learning rate for the DQN algorithm
        self.epsilon = 1  # exploration rate (initially set to 100%)
        self.epsilonDecay = 0.99  # rate at which to decay the exploration rate over time
        self.n_slices = 2  # number of slices to divide the network into
        self.n_discrete_states = 20  # number of discrete states per slice
        self.n_actions = 6  # number of possible actions for the agent
        self.lowest_Q_value = -2  # lowest possible Q value
        self.highest_Q_value = 0  # highest possible Q value

        # Setting initial values for the Q-model
        self.episode = 0  # current episode number
        self.discrete_state = [0, 0, 0]  # current discrete state of the environment
        self.new_discrete_state = [0, 0, 0]  # new discrete state after taking an action
        self.SlicesWithPackets = 0  # number of slices with packets
        self.current_state = [0, 0, 0]  # current state of the environment

        # Environment settings
        self.EPISODES = 20_000  # number of episodes to train the agent for
        self.ACTION_SPACE_SIZE = 22  # total number of possible actions (same as self.n_actions)
        self.SLICES = 3  # total number of slices in the network
        self.step = 10  # size of each time step
        self.weights = (  # possible weights for the different slices
            [0,0,0],
            [0, 0, 100],
            [0, 20, 80],
            [0, 40, 60],
            [0, 60, 40],
            [0, 80, 20],
            [0, 100, 0],
            [20, 80, 0],
            [40, 60, 0],
            [60, 40, 0],
            [80, 20, 0],
            [100, 0, 0],
            [80, 0, 20],
            [60, 0, 40],
            [40, 0, 60],
            [20, 0, 80],
            [60, 20, 20],
            [20, 60, 20],
            [20, 20, 60],
            [40, 40, 20],
            [40, 20, 40],
            [20, 40, 40],
        )

        # Exploration settings
        self.epsilon = 1  # the initial value for exploration rate, to be decayed over time
        self.EPSILON_DECAY = 0.9998  # slower exploration decay
        self.MIN_EPSILON = 0.001  # the minimum value for the exploration rate
        self.done = 0  # a flag to indicate if the simulation is finished
        self.isRandom = True  # a flag to indicate if the agent is taking random actions

        # Model
        self.agent = DQNAgent()  # initialize a DQNAgent object
        self.current_state = [0, 0, 0]  # initialize the current state of the simulation
        self.div = 1  # a factor used to adjust the simulation time step

        self.action = 0  # the current action taken by the agent
        self.reward = 0  # the current reward earned by the agent

        self.firstMeasuredBytes = [0, 0, 0]  # the number of bytes transmitted in each slice at the beginning of the simulation

        self.hadPackets = []  # a list to keep track of packets that have been transmitted

    def get_reward(self):
        # DQN state order remains:
        # 0 = eMBB
        # 1 = URLLC
        # 2 = mMTC

        embb_key = "eMBB"
        urllc_key = "URLLC"
        mmtc_key = "mMTC"

        # Service satisfaction per slice
        s_embb = self.set_NSRS(embb_key)
        s_urllc = self.set_NSRS(urllc_key)
        s_mmtc = self.set_NSRS(mmtc_key)

        # Resource-block utilization
        rbur_embb = self.set_RBUR(embb_key)
        rbur_urllc = self.set_RBUR(urllc_key)
        rbur_mmtc = self.set_RBUR(mmtc_key)

        efficiency = (rbur_embb + rbur_urllc + rbur_mmtc) / 3.0

        # URLLC packet-loss ratio over this decision interval
        urlcc_ues = self.slices[urllc_key].schedulerDL.ues

        sent_now = 0
        lost_now = 0

        for ue in urlcc_ues:
            flow = urlcc_ues[ue].packetFlows[0]
            sent_now += flow.sentPackets
            lost_now += flow.lostPackets

        delta_sent = sent_now - self.prev_urlcc_sent
        delta_lost = lost_now - self.prev_urlcc_lost

        self.prev_urlcc_sent = sent_now
        self.prev_urlcc_lost = lost_now

        if delta_sent > 0:
            plr_urllc = delta_lost / delta_sent
        else:
            plr_urllc = 0.0

        plr_urllc = min(max(plr_urllc, 0.0), 1.0)
        delivery_urllc = 1.0 - plr_urllc

        # Formal safety penalty:
        # state[1] corresponds to URLLC load
        weights = self.get_weights(self.action)

        safety_penalty = 0.0
        if self.current_state[1] >= 1 and weights[1] < 20:
            safety_penalty = 1.0

        reward = (
            0.25 * s_embb
            + 0.35 * s_urllc
            + 0.15 * s_mmtc
            + 0.15 * efficiency
            + 0.10 * delivery_urllc
            - 2.0 * safety_penalty
        )

        return reward

    def set_ue_throughput(self, slice):
        for ue in list(self.slices[slice].schedulerDL.ues.keys()):
            # Calculate the throughput for each user in the slice
            self.slices[slice].schedulerDL.ues[ue].throughput = (
                self.slices[slice].schedulerDL.ues[ue].sndbytes
                * 8000
                / (1024 * 1024 * self.granularity)
            )
            # Reset the sent bytes for the user
            self.slices[slice].schedulerDL.ues[ue].sndbytes = 0

    # Get the RBUR for each slice
    def set_RBUR(self, slice):
        # Get the number of packets transmitted in one second (pks_s) and the total
        # size of all the transmitted packets in one second (tbSize)
        pks_s = self.slices[slice].schedulerDL.pks_s
        tbSize = self.slices[slice].schedulerDL.tbSize

        # Reset the number of transmitted packets and the total size of packets
        self.slices[slice].schedulerDL.pks_s = 0
        self.slices[slice].schedulerDL.tbSize = 0

        # Calculate the RBUR based on the number of transmitted packets and the
        # total size of packets
        if (pks_s > tbSize and tbSize != 0) or pks_s == 0:
            return 1
        elif pks_s != 0 and tbSize == 0:
            return 0
        else:
            return pks_s / tbSize

    def normalize_state(self, state):
        return np.array(state, dtype=np.float32) / 500.0
    
    # Get the NSRS for each slice
    def set_NSRS(self, slice):
        # Count the number of users that are satisfied with their throughput
        users_satisfied = 0
        n_users = len(self.slices[slice].schedulerDL.ues.keys())

        # Calculate the throughput for each user in the slice
        self.set_ue_throughput(slice)

        # Check if the user's throughput meets the required throughput of the slice
        for ue in list(self.slices[slice].schedulerDL.ues.keys()):
            if (
                self.slices[slice].schedulerDL.ues[ue].throughput
                > self.slices[slice].reqThroughput
            ):
                users_satisfied += 1

        # Calculate the NSRS based on the number of satisfied users and the total number of users
        return users_satisfied / n_users

    def get_weights(self, action):
        # Get the weights for the given action
        return self.weights[action]

    def print_state2(self, state, weights, alloc, every=100):
        # Print the state, action, weights, allocation, and reward for every `every` episodes
        if self.episode % every == 0:
            print("State: ", state)
            print("Action: ", self.action, "Weight: ", weights, "allocation: ", alloc)
            print("Reward: ", self.reward)
            print("---------episeode: --------------------------", self.episode)

    def print_state(self, state, weights, alloc, every=100):
        # Print the state, action, weights, allocation, reward, new state, randomness,
        # and epsilon for every `every` episodes
        if self.episode % every == 0:
            print("State: ", state)
            print("Action: ", self.action, "Weight: ", weights, "allocation: ", alloc)
            print("Reward: ", self.reward)
            print("New State: ", self.new_state)
            print("Random: ", self.isRandom, "Epsilon: ", self.epsilon)
            print("---------episeode: --------------------------", self.episode)

    def resAlloc(self, env):
        if LEARNING:
            if RETRAIN:
                # If RETRAIN is set to True, load the saved model from 'models/{}'.format(MODEL_NAME)
                self.agent.load_model("models/{}".format(MODEL_NAME))

                # Synchronize target network with resumed main network
                self.agent.target_model.set_weights(
                    self.agent.model.get_weights()
                )

                # Resume from the last complete checkpoint
                self.episode = 9000

                # Restore epsilon corresponding to episode 9000
                self.epsilon = self.EPSILON_DECAY ** self.episode

                print("Resuming training from episode:", self.episode)
                print("Resumed epsilon:", self.epsilon)

            while True:
                # Write subframe number to a file
                self.dbFile.write("<h3> SUBFRAME NUMBER: " + str(env.now) + "</h3>")

                # Reset training variables
                self.new_state = []
                nSlice = 0
                alloc = []
                self.alloc = 0
                self.hadPackets = []

                # Choose an action
                if np.random.random() > self.epsilon:
                    # Get the best action from 'Q table'
                    self.action = np.argmax(self.agent.get_qs(self.normalize_state(self.current_state)))
                    bandera = True
                else:
                    # Choose a random action
                    self.action = np.random.randint(0, self.ACTION_SPACE_SIZE)
                    bandera = False

                self.isRandom = bandera

                # Get the set of weights for the previous action
                weights = self.get_weights(self.action)

                # Allocate resources for each slice
                for slice in list(self.slices.keys()):
                    # Calculate the number of PRBs allocated to the slice for the current action
                    prbs = int((self.PRBs * weights[nSlice]) / (100 * self.slices[slice].numRefFactor))

                    # Update the configuration of the slice
                    self.slices[slice].updateConfig(prbs)

                    # Add the number of PRBs allocated to the slice to the allocation list
                    alloc.append(prbs)

                    nSlice += 1

                self.alloc = alloc

                # Update epsilon
                if self.epsilon > self.MIN_EPSILON:
                    self.epsilon *= self.EPSILON_DECAY
                    self.epsilon = max(self.MIN_EPSILON, self.epsilon)

                # Wait for the transition time
                yield env.timeout(self.granularity)

                # Get the system state
                for slice in list(self.slices.keys()):
                    # Compute the number of packets sent in the current subframe by the slice and add it to the state
                    self.new_state.append(min(self.slices[slice].schedulerDL.updSumPcks() // self.div, 500))

                
                # Get the reward
                self.reward = self.get_reward()

                # Update the Q table
                self.agent.update_replay_memory(
                    (
                        self.current_state,
                        self.action,
                        self.reward,
                        self.new_state,
                        self.done,
                    )
                )
                self.agent.train(self.done, self.step)

                self.print_state(self.current_state, weights, alloc, SHOW_EVERY)

                # Update target network counter every episode
                last_state = self.current_state
                self.current_state = self.new_state

                # Update the step
                self.dbFile.write("<hr>")
                self.episode += 1

                # save the model every 1000 episodes

                if self.episode % 1000 == 0:
                    self.agent.model.save("models/{}".format(MODEL_NAME))
                    
        if LEARNING == False :
            # If LEARNING is False, predict the action only with the model but not learn.
            # Load the model from the file in 'models/{}'.format(MODEL_NAME)
            self.agent.load_model("models/{}".format(MODEL_NAME))
            
            # Set initial allocation to 0 for each slice
            self.alloc = [0,0,0]

            while True:
                # Write the current subframe number to the database file
                self.dbFile.write("<h3> SUBFRAME NUMBER: " + str(env.now) + "</h3>")
                
                # Reset training variables
                self.current_state = []
                nSlice = 0
                alloc = []

                # Get the system state for each slice
                for slice in list(self.slices.keys()):
                    # Update the state of the slice
                    self.current_state.append(
                        self.slices[slice].schedulerDL.updSumPcks() // self.div
                    )

                # Choose an action based on the predicted Q values from the model
                self.action = np.argmax(self.agent.get_qs(self.normalize_state(self.current_state))) 

                # Calculate the reward for the current state and action
                self.reward = self.get_reward()

                # Get the set of weights for the previous action
                weights = self.get_weights(self.action)

                # Perform the action chosen for each slice
                for slice in list(self.slices.keys()):
                    # Update the slice configuration with the allocation based on the weights
                    self.slices[slice].updateConfig(
                        (
                            int(
                                (self.PRBs * weights[nSlice])
                                / (100 * self.slices[slice].numRefFactor)
                            )
                        )
                    )
                    # Append the allocation for the slice to the alloc list
                    alloc.append(
                        (
                            int(
                                (self.PRBs * weights[nSlice])
                                / (100 * self.slices[slice].numRefFactor)
                            )
                        )
                    )
                    nSlice += 1

                # Set the allocation for the current state to alloc
                self.alloc = alloc

                # Print the system state, weights, and allocation if SHOW_EVERY conditions are met
                self.print_state2(self.current_state, weights, alloc, SHOW_EVERY)

                # Wait for the transition time
                yield env.timeout(self.granularity)


class DQNAgent:
    def __init__(self):
        self.DISCOUNT = 0.4  # discount factor for future rewards
        self.REPLAY_MEMORY_SIZE = 10000  # replay-buffer capacity
        self.MIN_REPLAY_MEMORY_SIZE = 500  # wait for enough transitions before training
        self.MINIBATCH_SIZE = 64  # training batch size
        self.UPDATE_TARGET_EVERY = 200  # target-network update interval
        self.MODEL_NAME = "formal_dqn_v2"  # formal DQN v2 model
        self.MIN_REWARD = -200  # minimum reward for model saving
        self.MEMORY_FRACTION = 0.20  # fraction of GPU memory to use for model training

        self.OBSERVATION_SPACE_VALUES = np.array([[20], [20], [20]])  # shape of the observation space
        self.ACTION_SPACE_SIZE = 22  # size of the action space

        # Main model
        self.model = self.create_model()

        # Target network
        self.target_model = self.create_model()
        self.target_model.set_weights(self.model.get_weights())

        # An array with last n steps for training
        self.replay_memory = deque(maxlen=self.REPLAY_MEMORY_SIZE)

        # Used to count when to update target network with main network's weights
        self.target_update_counter = 0

    # Creates our model
    def create_model(self):
        model = Sequential()
        model.add(Dense(32, activation="relu", input_dim=3))
        model.add(Dense(32, activation="relu"))
        model.add(Dense(self.ACTION_SPACE_SIZE, activation="linear"))
        model.compile(loss="mse", optimizer=Adam(learning_rate=0.001), metrics=["mae"])

        return model

    # function that loads the model
    def load_model(self, name):
        self.model = load_model(name)

    # Adds step's data to a memory replay array
    # (observation space, action, reward, new observation space, done)
    def update_replay_memory(self, transition):
        self.replay_memory.append(transition)

    # Trains main network every step during episode
    def train(self, terminal_state, step):
        # Start training only if certain number of samples is already saved
        if len(self.replay_memory) < self.MIN_REPLAY_MEMORY_SIZE:
            return

        # Get a minibatch of random samples from memory replay table
        minibatch = random.sample(self.replay_memory, self.MINIBATCH_SIZE)

        # Get current states from minibatch, then query NN model for Q values
        current_states = np.array([
            np.array(transition[0], dtype=np.float32) / 500.0
            for transition in minibatch
        ])

        current_qs_list = self.model(current_states, training=False).numpy()

        # Get future states from minibatch, then query NN model for Q values
        # When using target network, query it, otherwise main network should be queried
        new_current_states = np.array([
            np.array(transition[3], dtype=np.float32) / 500.0
            for transition in minibatch
        ])
        future_qs_list = self.target_model(
            new_current_states, training=False
        ).numpy()

        X = []
        y = []

        # Now we need to enumerate our batches
        for index, (
            current_state,
            action,
            reward,
            new_current_states,
            done,
        ) in enumerate(minibatch):
            # If not a terminal state, get new q from future states, otherwise set it to 0
            # almost like with Q Learning, but we use just part of equation here
            # if not done:
            max_future_q = np.max(future_qs_list[index])
            new_q = reward + self.DISCOUNT * max_future_q
            # else:
            #    new_q = reward

            # Update Q value for given state
            current_qs = current_qs_list[index]
            current_qs[action] = new_q

            # And append to our training data
            X.append(current_states[index])
            y.append(current_qs)

        # Fit on all samples as one batch, log only on terminal state
        self.model.train_on_batch(
            np.asarray(X, dtype=np.float32),
            np.asarray(y, dtype=np.float32),
        )

        # Update target network counter every episode

        self.target_update_counter += 1

        # If counter reaches set value, update target network with weights of main network
        if self.target_update_counter > self.UPDATE_TARGET_EVERY:
            self.target_model.set_weights(self.model.get_weights())
            self.target_update_counter = 0

    # Queries main network for Q values given current observation space (environment state)
    def get_qs(self, state):
        state = np.asarray(state, dtype=np.float32).reshape(1, -1)
        return self.model(state, training=False).numpy()[0]

